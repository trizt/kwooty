/***************************************************************************
 *   Copyright (C) 2013 by Xavier Lefage                                   *
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


#ifndef ACTIONFILEDELETEMANAGER_H
#define ACTIONFILEDELETEMANAGER_H

#include "actionfilemanagerbase.h"


class ActionFileDeleteManager : public ActionFileManagerBase {

    Q_OBJECT

public:
    ActionFileDeleteManager(ActionsManager*);

private:

    QStringList selectedItemUuidList;

    QString retrieveFileSavePath(QStandardItem*) const;
    bool isDeleteAllowed(QStandardItem*) const;
    void removeRowDeleteFile();
    void launchProcess();
    void resetState();

Q_SIGNALS:
    
public Q_SLOTS:


private Q_SLOTS:
    void handleResultSlot(KJob*);
    void actionTriggeredSlot();

};

#endif // ACTIONFILEDELETEMANAGER_H

