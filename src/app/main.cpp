/*
    Copyright (C) 2011, Steffen Müller.
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

#include "rackwindow.h"

#include <QtWidgets>
#include <QtSql>

int main(int argc, char *argv[])
{

    qDebug() << "welcome to r.a.c.k. version" << VER;

    //TODO: read carefully QApplication and Qt Namespace docs!!!!

    //QApplication::setStyle(QStyleFactory::create(QLatin1String("windows")));

    QApplication app(argc, argv);

    app.setApplicationName("r.a.c.k.");
    app.setApplicationDisplayName("r.a.c.k. - Radio Automation Construction Kit");
    app.setOrganizationName("Radio F.R.E.I.");
    app.setApplicationVersion("0.1");


    //connect to database:
    //TODO: settings from config
    QSqlDatabase db = QSqlDatabase::addDatabase("QPSQL");
    db.setHostName("localhost");
    db.setDatabaseName("rack");
    db.setUserName("postgres");
    db.setPassword("");
    if (!db.open())
    {
//        QMessageBox::critical(0, qApp->tr("Cannot open database"),
//                              qApp->tr("Unable to establish a database connection.\n"
//                                       "Click Cancel to exit."), QMessageBox::Cancel);
//        return 1;
        qDebug() << "no database connection";
        qDebug() << db.lastError();
    }

    QStringList driverList;
    driverList = QSqlDatabase::drivers();
    foreach (QString driver, driverList) {
        qDebug() << driver;
    }



    RackWindow mainWindow;

    mainWindow.resize(800, 600);
//  mainWindow.setWindowState(Qt::WindowMaximized);

    mainWindow.show();

    return app.exec();
}
