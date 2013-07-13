/*
    Copyright (C) 2011, Steffen Müller and the r.a.c.k. team.
    All rights reserved.

    This file is part of r.a.c.k. radio automation construction kit

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    Author: Steffen Müller
*/


#include <QCoreApplication>
#include <QDir>
#include <QtConcurrent/QtConcurrent>
#include <QUdpSocket>
#include <QColor>
#include <QPainter>

#include <signal.h>

#include "rackd.h"
#include "rackdclientsocket.h"


//TODO: protocol for device information/init
//TODO: send waveform

//TODO: crash if we quit while streamloadurl thread is running

//TODO: use floating point channels (see bass docs)

//TODO: if connections count is null go to auto modus ???
//TODO: qsettings
//loglevels?

//TODO: abstraction for sound stuff (make it possible to use other sound frameworks like gstreamer or ecasound)

//TODO: meterupdate timer could remove autofree streams from m_streams instead of use bass syncproc ??????

//TODO use QtConcurrent::mappedReduced() instead of QtConcurrent::run() for cancel ????? check this!


void SigHandler(int signum)
{
  switch(signum)
  {
      case SIGINT:
      case SIGTERM:
      case SIGHUP:
      qDebug() << "signal:" << signum << "received";
        qApp->quit();
      break;
  }
}


Rackd::Rackd(QObject *parent)
    : QTcpServer(parent),
      m_maxConnections(32),
      m_createUrlWatcher(new QFutureWatcher<RThreadFunctionData>(this)),
      m_waveformWatcher(new QFutureWatcher<RThreadFunctionData>(this)),
      m_meterSocket(new QUdpSocket(this)),
      m_lastBassError(0)
{
    qDebug() << "Hello from rack daemon!";

    signal(SIGHUP,SigHandler);
    signal(SIGINT,SigHandler);
    signal(SIGTERM,SigHandler);

    connect(m_createUrlWatcher, SIGNAL(finished()), this, SLOT(loadStreamFinished()));
    connect(m_waveformWatcher, SIGNAL(finished()), this, SLOT(generateWaveformFinished()));


    //audio devices:
    BASS_DEVICEINFO info;
    for (int i = 0; BASS_GetDeviceInfo(i, &info); i++) if (info.flags&BASS_DEVICE_ENABLED)
    {
        if (QString::compare(info.name,"Default") == 0) continue;

        qDebug() << i << "audio device name:" << info.name << info.driver;

        int flag;
        i == 0 ? flag = BASS_DEVICE_MONO : flag = 0;

        if (BASS_Init(i, 44100, flag, 0, NULL))
        {
            qDebug() << "initialized";
            m_devices.append(i);
        }
    }

    //start listen:
    if (!listen(QHostAddress::Any, 1234))
    {
        qDebug() << "Unable to start tcp server!";
        qDebug() << "Error is:" << errorString();
        qDebug() << "rack deamon exiting";
        exit(1);
    }
    else
    {
        qDebug() << "waiting for incoming connections..." << serverAddress().toString() << serverPort();
    }

    //udp meter status timer:
    m_timer.start(50, this);

}


void Rackd::incomingConnection(qintptr socketId)
{

    qDebug() << "new incomming connection";

    RackdClientSocket *client = new RackdClientSocket(this);
    client->setSocketDescriptor(socketId);

    if (m_clients.count() == m_maxConnections)
    {
        qDebug() << "max connections (" << m_clients.count() << ") reached";
        qDebug() << "reject client";
        connect(client, SIGNAL(disconnected()), client, SLOT(deleteLater()));
        client->disconnectFromHost();
        client = 0;
        return;
    }

    m_clients.append(client);

    connect(client, SIGNAL(newBlock(RackdClientSocket*,QByteArray)), this, SLOT(handleRequest(RackdClientSocket*,QByteArray)));
    connect(client, SIGNAL(disconnectedClient(RackdClientSocket*)), this, SLOT(clientDisconnected(RackdClientSocket*)));
    connect(client, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(handleError(QAbstractSocket::SocketError)));

    qDebug() << "new client connected from" << client->peerAddress().toString() << client->peerPort();
    qDebug() << "we have now" << m_clients.count() << "connections";

}


void Rackd::clientDisconnected(RackdClientSocket *client)
{
    //clients have to free resources (unload streams) before disconnect
    //if we have resources here we asume a client crash or drop connection without
    //unload streams. unload all client streams except currently playing, which
    //we switch to autofree.

    //we need to iterate backwards because we use removeAt(i)
    const int size = m_streams.size() - 1;
    for (int i = size; i >= 0; --i)
    {
        if (m_streams.at(i).client == client)
        {
            if (BASS_ChannelIsActive(m_streams.at(i).handle) == BASS_ACTIVE_PLAYING)
            {
                BASS_ChannelFlags(m_streams.at(i).handle, BASS_STREAM_AUTOFREE, BASS_STREAM_AUTOFREE);
                BASS_ChannelSetSync(m_streams.at(i).handle, BASS_SYNC_FREE, 0, &Rackd::freeSyncProc, this);
                m_streams[i].client = 0;
            }
            else
            {
                BASS_StreamFree(m_streams.at(i).handle);
                m_streams.removeAt(i);
            }
        }
    }

    m_clients.removeAll(client);

    qDebug() << "client disconnected" << client->peerAddress().toString() << client->peerPort();
    qDebug() << "we have now" << m_clients.count() << "connections";

    qDebug() << "active streams:";
    foreach (RStreamData streamData, m_streams) qDebug() << streamData.handle << streamData.client << streamData.device;

    client->deleteLater();
}


//TODO:
void Rackd::handleError(QAbstractSocket::SocketError)
{
    qDebug() << errorString();
}


//rackd protocoll implementation:

void Rackd::handleRequest(RackdClientSocket *client, const QByteArray &request)
{

    QDataStream requestDS(request);
    requestDS.setVersion(QDataStream::Qt_5_0);

    //prepare response block:
    QByteArray response;
    QDataStream responseDS(&response, QIODevice::WriteOnly);
    responseDS.setVersion(QDataStream::Qt_5_0);
    responseDS << quint32(0);

    QString command;
    requestDS >> command;

    qDebug() << "command is:" << command;

    if (command == "PW")
    {
        QString pw;
        requestDS >> pw;
        bool ok = (pw == "pass"); //TODO: read from settings
        client->setAuth(ok);
        responseDS << command << ok;

        qDebug() << "client" <<  client->peerAddress().toString()  << client->peerPort() << "authenticated:" << client->isAuth();

        responseDS.device()->seek(0);
        responseDS << quint32(response.size() - sizeof(quint32));
        sendResponse(client, response);
        return;
    }

    if (command == "DC")
    {
        client->disconnectFromHost();
        return;
    }


    //the following requests needs authentication:

    if (!client->isAuth())
    {

        qDebug() << "ERROR: authentication required!";

        responseDS << QString("ER") << command << QString("request need authentication");
        responseDS.device()->seek(0);
        responseDS << quint32(response.size() - sizeof(quint32));
        sendResponse(client, response);
        return;
    }

    if (command == "LS")
    {
        quint8 device;
        QString uri;
        requestDS >> device >> uri;

        qDebug() << uri;

        HSTREAM handle;

        if (uri.startsWith("http://", Qt::CaseInsensitive) || uri.startsWith("ftp://", Qt::CaseInsensitive))
        {
            //load stream from url in another thread:
            RThreadFunctionData data;

            data.client = client;


            data.device = device;
            data.uri = uri;
            QFuture<RThreadFunctionData> f = QtConcurrent::run(this, &Rackd::loadStreamInThread, data);
            m_createUrlWatcher->setFuture(f);
            return;
        }
        else
        { 
            QString absFileName = QDir::cleanPath(uri);
            BASS_SetDevice(device);
            handle = BASS_StreamCreateFile(false, qPrintable(absFileName), 0, 0, 0);
        }

        if (handle)
        {

            qDebug() << "load stream ok:" << uri;

            RStreamData streamData = {handle, client, device, 0};
            m_streams.append(streamData);

            qDebug() << "active streams:";
            foreach (RStreamData streamData, m_streams) qDebug() << streamData.handle << streamData.client << streamData.device;

            //get the play time:
            quint32 time;
            qint64 t = qint64(BASS_ChannelBytes2Seconds(handle, BASS_ChannelGetLength(handle, BASS_POS_BYTE))*1000);
            (t > 0) ? time = quint32(t) : time = 0;

            responseDS << command << device << uri << quint32(handle) << time << true;
        }
        else
        {
            qDebug() << "ERROR: load stream failed:" << uri << BASS_ErrorGetCode();
            responseDS << command << device << uri << quint32(handle) << quint32(0) << false;
        }

        responseDS.device()->seek(0);
        responseDS << quint32(response.size() - sizeof(quint32));
        sendResponse(client, response);
        return;
    }

    if (command == "US")
    {
        quint32 handle;
        requestDS >> handle;
        bool ok = BASS_StreamFree(handle);
        responseDS << command << handle << ok;

        qDebug() << "unload stream:" << handle << ok << BASS_ErrorGetCode();

        if (ok)
        {
            //we need to iterate backwards because we use removeAt(i)
            const int size = m_streams.size() - 1;
            for (int i = size; i >= 0; --i)
            {
                if (m_streams.at(i).handle == handle) m_streams.removeAt(i);
            }
        }

        qDebug() << "active streams:";
        foreach (RStreamData streamData, m_streams) qDebug() << streamData.handle << streamData.client << streamData.device;

        responseDS.device()->seek(0);
        responseDS << quint32(response.size() - sizeof(quint32));
        sendResponse(client, response);
        return;
    }

    //TODO: check if position is out of range? (or is this clients job?)
    if (command == "PP")
    {
        quint32 handle;
        quint32 position;
        requestDS >> handle >> position;
        bool ok = BASS_ChannelSetPosition(handle, BASS_ChannelSeconds2Bytes(handle, position/1000), BASS_POS_BYTE);
        responseDS << command << handle << ok;

        qDebug() << "seek:" << handle << position << ok << BASS_ErrorGetCode();

        responseDS.device()->seek(0);
        responseDS << quint32(response.size() - sizeof(quint32));
        sendResponse(client, response);
        return;
    }

    if (command == "PY")
    {
        quint32 handle;
        //quint32 length;
        //quint16 speed;
        //bool pitch;
        requestDS >> handle;
        bool ok = BASS_ChannelPlay(handle, false);
        responseDS << command << handle << ok;

        qDebug() << "play:" << handle << ok << BASS_ErrorGetCode();

        responseDS.device()->seek(0);
        responseDS << quint32(response.size() - sizeof(quint32));
        sendResponse(client, response);
        return;
    }


    //TODO: pause vs stop ????
    if (command == "SP")
    {
        quint32 handle;
        requestDS >> handle;
        bool ok = BASS_ChannelPause(handle);
        responseDS << command << handle << ok;

        qDebug() << "stop:" << handle << ok << BASS_ErrorGetCode();

        responseDS.device()->seek(0);
        responseDS << quint32(response.size() - sizeof(quint32));
        sendResponse(client, response);
        return;
    }


    //    //EI
    //    if (!qstrcmp(commandList[0], "EI") && commandList.size() == 2)
    //    {
    //        qDebug() << BASS_GetConfig(BASS_CONFIG_HANDLES);
    //        return;
    //    }



    //    TS <card-num>!
    //    LR <card-num> <port-num> <coding> <channels> <samp-rate> <bit-rate>
    //    UR <card-num> <stream-num>!
    //    RD <card-num> <stream-num> <length> <threshold>!
    //    RS <card-num> <stream-num>!
    //    SR <card-num> <stream-num>!
    //    IV <card-num> <stream-num> <level>!
    //    OV <card-num> <stream-num> <port-num> <level>!
    //    FV <card-num> <stream-num> <port-num> <level> <length>!
    //    IL <card-num> <port-num> <level>!
    //    OL <card-num> <port-num> <level>!
    //    IM <card-num> <stream-num> <mode>!
    //    OM <card-num> <stream-num> <model>!
    //    IX <card-num> <stream-num> <level>!
    //    IT <card-num> <port-num> <type>!
    //    IS <card-num> <port-num>!
    //    AL <card-num> <input-num> <output-num> <level>!
    //    CO <card-num> <port-num> <udp-port> <samp-rate> <chans>!
    //    JC <output_name> | <input_name>!
    //    JD <output_name> | <input_name>!

    if (command == "WF")
    {
        quint32 handle;
        requestDS >> handle;

        qDebug() << "wave form:" << client << handle;

        //generate waveform in another thread:
        RThreadFunctionData data;
        data.client = client;
        data.handle = handle;
        QFuture<RThreadFunctionData> f = QtConcurrent::run(this, &Rackd::generateWaveformInThread, data);
        m_waveformWatcher->setFuture(f);
        return;
    }

    if (command == "ME")
    {
        quint16 port;
        requestDS >> port;
        client->setClientMeterPort(port);
        responseDS << command << port << true;

        qDebug() << "meter enable:" << client << port;

        responseDS.device()->seek(0);
        responseDS << quint32(response.size() - sizeof(quint32));
        sendResponse(client, response);
        return;
    }

    //    ML <type> <card-num> <port-num> <left-lvl> <right-lvl>!
    //    MO <card-num> <stream-num> <left-lvl> <right-lvl>!
    //    MP <card-num> <stream-num> <pos>!
    //    MS <card-num> <port-num> <stream-num> <status>!


    qDebug() << "ERROR: unknown request" << command;
    responseDS << QString("ER") << command << QString("unknown request");
    responseDS.device()->seek(0);
    responseDS << quint32(response.size() - sizeof(quint32));
    sendResponse(client, response);

}


// function runs in another thread:
RThreadFunctionData Rackd::loadStreamInThread(RThreadFunctionData &data)
{
    BASS_SetDevice(data.device);
    HSTREAM handle = BASS_StreamCreateURL(qPrintable(data.uri), 0, 0, NULL, 0);
    if (handle)
    {
        qDebug() << "load stream url ok:" << data.uri << handle;
        data.handle = handle;
        qint64 t = qint64(BASS_ChannelBytes2Seconds(handle, BASS_ChannelGetLength(handle, BASS_POS_BYTE))*1000);
        (t > 0) ? data.time = quint32(t) : data.time = 0;
    }
    else
    {
        qDebug() << "ERROR: load stream url failed:" << data.uri << BASS_ErrorGetCode();
        data.handle = 0;
    }
    data.ok = bool(handle);
    return data;
}


//slot called when loadstream thread is finnished:
void Rackd::loadStreamFinished()
{
    RThreadFunctionData data = m_createUrlWatcher->result();

    //check if the client pointer is valid anymore (since we was in another thread the connection may be dropped)
    if (!m_clients.contains(data.client))
    {
        qDebug() << "client that requested load url stream is disconnected";
        bool ok = BASS_StreamFree(data.handle);
        qDebug() << "unload url stream:" << data.handle << ok;
        return;
    }

    RStreamData streamData = {data.handle, data.client, data.device, 0};
    m_streams.append(streamData);
    QByteArray response;
    QDataStream responseDS(&response, QIODevice::WriteOnly);
    responseDS.setVersion(QDataStream::Qt_5_0);
    responseDS << quint32(0) << QString("LS") << data.device << data.uri << data.handle << data.time << data.ok;
    responseDS.device()->seek(0);
    responseDS << quint32(response.size() - sizeof(quint32));
    sendResponse(data.client, response);
}


// function runs in another thread:
RThreadFunctionData Rackd::generateWaveformInThread(RThreadFunctionData &data)
{
    quint16 width = 500;
    quint16 height = 60;
    QColor color("#4175FF");
    bool aa = true;
    BASS_CHANNELINFO info;
    DWORD decoder = 0;

    if (BASS_ChannelGetInfo(data.handle, &info))
    {
        BASS_SetDevice(0);
        decoder = BASS_StreamCreateFile(FALSE, info.filename, 0, 0, BASS_STREAM_DECODE);
    }

    if (decoder)
    {

        qDebug() << "prepare wave form decode stream ok:" << data.handle;

        QImage waveform = QImage(width, height, QImage::Format_ARGB32_Premultiplied);
        DWORD bytesPerPixel = BASS_ChannelGetLength(decoder, BASS_POS_BYTE) / width;
        if (bytesPerPixel < BASS_ChannelSeconds2Bytes(decoder, 0.02)) bytesPerPixel = BASS_ChannelSeconds2Bytes(decoder, 0.02);
        QPainter painter(&waveform);
        painter.setPen(color);
        if (aa) painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(waveform.rect(), Qt::black);
        DWORD level = 0, peak = 0, scanPos = 0, imagePos = 0, line = 0;
        while (BASS_ChannelIsActive(decoder) == BASS_ACTIVE_PLAYING)
        {
            level = BASS_ChannelGetLevel(decoder);
            if (peak < LOWORD(level)) peak = LOWORD(level);
            scanPos = BASS_ChannelGetPosition(decoder, BASS_POS_BYTE) / bytesPerPixel;
            if (scanPos > imagePos)
            {
                line = peak * (height - 8) / 32768;
                painter.drawLine(imagePos, (height - line) / 2, imagePos, (height - line) / 2 + line);
                imagePos = scanPos;
                peak = 0;
            }
        }
        data.waveform = waveform;

        qDebug() << "generate wave form ok:" << data.handle;

        BASS_StreamFree(decoder);
    }
    else
    {
        qDebug() << "ERROR: generate waveform failed:" << data.handle << BASS_ErrorGetCode();
    }
    data.ok = bool(decoder);
    return data;
}


//slot called when generate waveform thread is finnished:
void Rackd::generateWaveformFinished()
{
    RThreadFunctionData data = m_waveformWatcher->result();

    //check if the client pointer is valid anymore (since we was in another thread the connection may be dropped)
    if (!m_clients.contains(data.client))
    {
        qDebug() << "client that requested wave form is disconnected";
        return;
    }

    QByteArray response;
    QDataStream responseDS(&response, QIODevice::WriteOnly);
    responseDS.setVersion(QDataStream::Qt_5_0);
    responseDS << quint32(0) << QString("WF") << data.handle << data.waveform << data.ok;
    responseDS.device()->seek(0);
    responseDS << quint32(response.size() - sizeof(quint32));
    sendResponse(data.client, response);
}


void Rackd::sendResponse(RackdClientSocket *client, const QByteArray &response)
{


    if (client->state() == QAbstractSocket::ConnectedState)
    {
        //qDebug() << "send response block:" << response.toHex() << "size (Bytes):" << response.size();
        client->write(response);
    }
}


//meter status update:
void Rackd::timerEvent(QTimerEvent *)
{
    const int sSize = m_streams.size();
    for (int i = 0; i < sSize; ++i)
    {
        QByteArray datagram;
        QDataStream out(&datagram, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_5_0);
        qint64 pos = qint64(BASS_ChannelBytes2Seconds(m_streams.at(i).handle, BASS_ChannelGetPosition(m_streams.at(i).handle, BASS_POS_BYTE))*1000);
        (pos > 0) ? m_streams[i].position = quint32(pos) : m_streams[i].position = 0;
        out << QString("MP") << m_streams.at(i).device << m_streams.at(i).handle << m_streams.at(i).position;

        //qDebug() << data.client << data.handle << data.position;

        const int cSize = m_clients.size();
        for (int i = 0; i < cSize; ++i)
        {
            if (m_clients.at(i)->clientMeterPort() > 0)
            {
                m_meterSocket->writeDatagram(datagram, m_clients.at(i)->peerAddress(), m_clients.at(i)->clientMeterPort());
                //qDebug() << "send" << m_clients.at(i)->peerPort();
            }
        }
    }

    if (BASS_ErrorGetCode() == m_lastBassError) return;
    m_lastBassError = BASS_ErrorGetCode();
    qDebug () << m_lastBassError;

}


// bass callback functions:
void CALLBACK Rackd::freeSyncProc(HSYNC, DWORD handle, DWORD, void *ptr2rack)
{
    Rackd* rack = (Rackd*)ptr2rack;

    //we need to iterate backwards because we use removeAt(i)
    const int size = rack->m_streams.size() - 1;
    for (int i = size; i >= 0; --i)
    {
        if (rack->m_streams[i].handle == handle)
        {
            rack->m_streams.removeAt(i);
        }
    }

    qDebug() << "active streams:";
    foreach (RStreamData streamData, rack->m_streams) qDebug() << streamData.handle << streamData.client << streamData.device;

}


void Rackd::doCleanUp()
{
    qDebug() << "clean up called";
    qDebug() << "stop listen for incoming connections";
    close();

    foreach (int i, m_devices) if (BASS_SetDevice(i))
    {
        BASS_DEVICEINFO info;
        BASS_GetDeviceInfo(i, &info);
        if (BASS_Free()) qDebug() << i << "audio device" << info.driver << "deinitialized";
    }
}


Rackd::~Rackd()
{
    qDebug() << "destructor called. Goodbye from rack daemon!";
}



