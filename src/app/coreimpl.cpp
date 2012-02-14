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

#include "coreimpl.h"
#include "rackwindow.h"

#include <QTime>
#include <QFileSystemModel>


CoreImpl::CoreImpl(RackWindow *mainwindow) :
    m_mainwindow(mainwindow),
    m_state(NormalState),
    m_fileSystemModel(new QFileSystemModel(this))
{

    //create models:
    QStringList filters;
    filters << "*.wav" << "*.ogg" << "*.aiff" << "*.mp3" << "*.mp2" << "*.mp1" << "*.mus";
    m_fileSystemModel->setRootPath("/");
    m_fileSystemModel->setNameFilters(filters);
    m_fileSystemModel->setNameFilterDisables(false);
    m_fileSystemModel->sort(Qt::AscendingOrder);

    m_modelList.append(m_fileSystemModel);


    startTimer(1000);
}

CoreImpl::~CoreImpl()
{

}

ICore::State CoreImpl::state() const
{
    return m_state;
}

void CoreImpl::setNormalState()
{
    m_state = NormalState;
    emit normalStateEntered();
}

void CoreImpl::setInsertState()
{
    m_state = InsertState;
    emit insertStateEntered();
}

void CoreImpl::setPreviewState()
{
    m_state = PreviewState;
    emit previewStateEntered();
}

void CoreImpl::setDeleteState()
{
    m_state = DeleteState;
    emit deleteStateEntered();
}


QList<QAbstractItemModel *> CoreImpl::modelList() const
{
    return m_modelList;
}

void CoreImpl::timerEvent(QTimerEvent *)
{
    emit timeStrChanged(QTime::currentTime().toString(tr("hh:mm:ss")));
    emit dateStrChanged(QDate::currentDate().toString(tr("dd.MM.yy")));
}

void CoreImpl::getHello(const QString &str)
{
    m_mainwindow->setWindowTitle(str);
}
