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

#ifndef SERVERGROUP_H
#define SERVERGROUP_H

#include <QObject>
#include <QTimer>

#include "data/serverdata.h"
#include "data/segmentdata.h"
#include "utilities/utility.h"
using namespace UtilityNamespace;

class ClientManagerConn;
class ServerManager;
class Core;
class ClientsPerServerObserver;
class ServerSpeedManager;

class ServerGroup : public QObject
{

    Q_OBJECT

public:
    ServerGroup(ServerManager *, Core *, int);
    Core *getCore();
    ServerManager *getServerManager();
    ClientsPerServerObserver *getClientsPerServerObserver();
    ServerSpeedManager *getServerSpeedManager();
    ServerGroup *getNextTargetServer();
    ServerData getServerData() const;
    QList<ClientManagerConn *> getClientManagerConnList();
    int getServerGroupId() const;
    int getRealServerGroupId() const;
    int saveSegment(const SegmentData &);
    bool isBufferFull();
    bool canDownload(int) const;
    bool isServerAvailable() const;
    bool isMasterServer() const;
    bool isDisabledBackupServer() const;
    bool isPassiveBackupServer() const;
    bool isActiveBackupServer() const;
    bool isFailoverBackupServer() const;
    bool isPassiveFailover() const;
    bool isActiveFailover() const;
    void assignDownloadToReadyClients();
    void disconnectAllClients();
    void connectAllClients();
    void resetAllClientsConnection();
    void readDataWithPassword();

private:
    static const int MAX_SERVER_DOWN_PER_MINUTE = 4;

    ServerData mServerData;
    QTimer *mClientsAvailableTimer;
    QTimer *mStabilityTimer;
    QList<ClientManagerConn *> mClientManagerConnList;
    Core *mCore;
    ServerManager *mServerManager;
    ServerSpeedManager *mServerSpeedManager;
    ClientsPerServerObserver *mClientsPerServerObserver;
    int mServerGroupId;
    int mStabilityCounter;
    bool mServerAvailable;
    bool mPendingSegments;
    bool mPasswordRetrieved;

    void createNntpClients();
    void setupConnections();
    void serverSwitchIfFailure();

Q_SIGNALS:
    void dataHasArrivedClientReadySignal();
    void disconnectRequestSignal();
    void connectRequestSignal();
    void resetConnectionSignal();

public Q_SLOTS:
    bool settingsServerChangedSlot();
    void downloadPendingSegmentsSlot();

private Q_SLOTS:
    void checkServerAvailabilitySlot();
    void checkServerStabilitySlot();
    void startTimerSlot();

};

#endif // SERVERGROUP_H
