/***************************************************************************
 *   Copyright (C) 2010 by Xavier Lefage                                   *
 *   xavier.kwooty@gmail.com                                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/


#ifndef SERVERPREFERENCESWIDGET_H
#define SERVERPREFERENCESWIDGET_H

#include <QWidget>

#include "ui_serversettings.h"
#include "data/serverdata.h"
#include "servertabwidget.h"

class PreferencesServer;
class ServerTabWidget;


class ServerPreferencesWidget : public QWidget {

    Q_OBJECT


public:
    ServerPreferencesWidget(ServerTabWidget*, PreferencesServer*, int, const ServerTabWidget::ServerNameQuery);
    ~ServerPreferencesWidget();

    void setData(const int&);
    ServerData getData();
    int getTabIndex();
    void setGroupBoxTitle(const int&);

private:
    Ui_ServerSettings* serverSettingsUi;
    PreferencesServer* preferencesServer;
    ServerTabWidget* serverTabWidget;
    int tabIndex;

    bool checkFormText(const QString&);
    void setupConnections();
    void setupButtons();
    void hideWidgets(const int&) ;
    void enableWidgets(const bool&);
    void fillWarningLabel(QLabel*, const QString&);
    void clearWarningLabel(QLabel*);


Q_SIGNALS:

public Q_SLOTS:

private Q_SLOTS:
    void portValueChangedSlot(int);
    void valueChangedSlot();
    void serverModeValueChangedSlot(int);
    void pushButtonInfoClickedSlot();
    void formEditingFinishedSlot();



};

#endif // SERVERPREFERENCESWIDGET_H
