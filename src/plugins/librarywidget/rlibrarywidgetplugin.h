/*
    Copyright (C) 2012, Steffen Müller and the r.a.c.k. team.
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

#ifndef RLIBRARYWIDGETPLUGIN_H
#define RLIBRARYWIDGETPLUGIN_H

#include "iwidgetplugin.h"
#include <QObject>

class RLibraryWidgetPlugin : public QObject, IWidgetPlugin
{
    Q_OBJECT
    Q_INTERFACES(IWidgetPlugin)

public:

    QWidget *createRWidget(ICore *api, QWidget *parent = 0);

    QString name() const { return "Library Widget"; }
    QString description() const { return tr("The Library Widget permits access to playable Items."); }
    QString version() const { return "0.0.1"; }
    QString url() const { return "http://rack-radio-automation-construction-kit.googlecode.com/"; }
    QString vendor() const { return "rack"; }
    QString license() const { return "GNU GPLv3+"; }
    QString copyright() const { return "Copyright (C) Steffen Müller"; }
    
};

#endif // RLIBRARYWIDGETPLUGIN_H
