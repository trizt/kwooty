/***************************************************************************
 *   Copyright (C) 2009 by Xavier Lefage                                   *
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

#include "nntpclient.h"

#include <KDebug>
#include <QTimer>
#include <QSslCipher>
#include <QFile>

#include "servergroup.h"
#include "data/serverdata.h"
#include "kwootysettings.h"
#include "clientmanagerconn.h"
#include "servermanager.h"



NntpClient::NntpClient(ClientManagerConn* parent) : QObject (parent) {

    this->parent = parent;

    // instantiate socket :
    tcpSocket = new QSslSocket(parent);
    tcpSocket->setPeerVerifyMode(QSslSocket::QueryPeer);

    // set a timer to reconnect to host after 10 seconds if disconnection occurs :
    tryToReconnectTimer = new QTimer(this);
    tryToReconnectTimer->setInterval(10000);

    // set a timer to disconnect from host after idle activity :
    idleTimeOutTimer = new QTimer(this);
    idleTimeOutTimer->setInterval(parent->getServerData().getDisconnectTimeout() * UtilityNamespace::MINUTES_TO_MILLISECONDS);

    // set a timer to check that stream communication is not stuck,
    // disconnect from host after 20 seconds with no answer from host :
    serverAnswerTimer = new QTimer(this);
    serverAnswerTimer->setInterval(20000);
    serverAnswerTimer->setSingleShot(true);

    this->authenticationDenied = false;
    this->segmentProcessed = false;
    this->postingOk = false;
    this->nntpError = NoError;

    // set up connections with tcpSocket :
    this->setupConnections();

    // set client status to IdleStatus by default :
    this->setConnectedClientStatus(ClientIdle);

    this->connectToHost();

    // notify status bar that SSL is disabled by default :
    emit encryptionStatusSignal(false);

}




NntpClient::~NntpClient() {
    // stop all timers :
    idleTimeOutTimer->stop();
    tryToReconnectTimer->stop();
    serverAnswerTimer->stop();

    // quit :
    this->segmentProcessed = true;
    this->sendQuitCommandToServer();
    this->segmentDataRollBack();
    tcpSocket->abort();

}


void NntpClient::setupConnections() {

    connect (tcpSocket, SIGNAL(readyRead()), this, SLOT(readyReadSlot()));
    connect (tcpSocket, SIGNAL(connected()), this, SLOT(connectedSlot()));
    connect (tcpSocket, SIGNAL(disconnected()), this, SLOT(disconnectedSlot()));
    connect (tcpSocket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(errorSlot(QAbstractSocket::SocketError)));
    connect (tcpSocket, SIGNAL(encrypted()), this, SLOT(socketEncryptedSlot()));
    connect (tcpSocket, SIGNAL(peerVerifyError(const QSslError&)), this, SLOT(peerVerifyErrorSlot()));

    // timer connections :
    connect(tryToReconnectTimer, SIGNAL(timeout()), this, SLOT(tryToReconnectSlot()));
    connect(idleTimeOutTimer, SIGNAL(timeout()), this, SLOT(idleTimeOutSlot()));
    connect(serverAnswerTimer, SIGNAL(timeout()), this, SLOT(answerTimeOutSlot()));

}


void NntpClient::connectToHost() {

    if (this->parent->isMasterServer() ||
        !this->parent->isDisabledBackupServer()) {

        // set nntpError to noError by default before connection process :
        this->postingOk = false;
        this->nntpError = NoError;

        //kDebug() << "client ID : " << parent->getClientId() << "disconnectTimeout : " << Settings::disconnectTimeout();
        idleTimeOutTimer->stop();
        idleTimeOutTimer->setInterval(parent->getServerData().getDisconnectTimeout() * UtilityNamespace::MINUTES_TO_MILLISECONDS);

        //kDebug() << " server Number : " << parent->getServerGroup()->getServerGroupId();

        QString hostName = parent->getServerData().getHostName();
        int port = parent->getServerData().getPort();

        if (parent->getServerData().isEnableSSL()) {

            // by default, consider that certificate has been verified.
            // It could be set to false if peerVerifyErrorSlot() is raised
            //tcpSocket->setPeerVerifyMode(QSslSocket::AutoVerifyPeer);
            this->certificateVerified = true;

            tcpSocket->connectToHostEncrypted(hostName, port);
        }
        else {
            tcpSocket->connectToHost(hostName, port);
            // SSL is disabled :
            emit encryptionStatusSignal(false);
        }

   }

}


void NntpClient::getAnswerFromServer() {

    // get answer from server :
    int answer = tcpSocket->readLine().left(3).toInt();

    switch (answer) {

    case ServerIsReadyPosting: case ServerIsReadyNoPosting: {

            this->setConnectedClientStatus(ClientIdle);

            // wait that server returns posting ok before requesting a segment to download :
            this->postingOk = true;
            this->requestNewSegment();
            break;
        }

    case AuthenticationRequired: {
            // authentication name requested :
            serverAnswerTimer->stop();
            if (parent->getServerData().isAuthentication()) {
                this->sendUserCommandToServer();
            }
            else{
                // group box is unchecked but authentication needed, inform the user :
                nntpError = AuthenticationNeeded;
                this->authenticationDenied = true;

                this->sendQuitCommandToServer();
            }
            break;
        }

    case PasswordRequested: {
            // authentication password requested :
            serverAnswerTimer->stop();
            if (parent->getServerData().isAuthentication()) {
                this->sendPasswordCommandToServer();
            }
            else{
                // group box is uncheked but authentication needed, inform the user :
                nntpError = AuthenticationNeeded;
                this->authenticationDenied = true;

                this->sendQuitCommandToServer();               
            }
            break;
        }

    case AuthenticationAccepted: {
            //kDebug() << "answer from server : " << answer;
            this->authenticationDenied = false;

            this->sendBodyCommandToServer();
            break;
        }

    case AuthenticationDenied: case AuthenticationRejected: {
            //kDebug() << "answer from server : " << answer;

            // stop reconnect timer if authentication has been rejected :
            serverAnswerTimer->stop();

            this->authenticationDenied = true;

            // set type of error in order to notify status bar :
            nntpError = AuthenticationFailed;

            // disconnect from host :
            this->sendQuitCommandToServer();
            tcpSocket->abort();

            // In case of authentication denied, try to reconnect to host in 30 seconds :
            //TODO :
            if (!this->parent->isMasterServer()) {
                QTimer::singleShot(300000, this, SLOT(answerTimeOutSlot()) );
            }
            else {
                QTimer::singleShot(30000, this, SLOT(answerTimeOutSlot()) );
            }
            kDebug() << "AuthenticationDenied,  try to reconnect in 30 seconds" << "group :" << parent->getServerGroup()->getServerGroupId();;
            break;
        }

    case BodyArticleFollows: {
            // init before download :
            this->setConnectedClientStatus(ClientDownload);

            // set file identifier in order to delete incomplete downloads at next launch :
            this->segmentByteArray.clear();
            this->segmentByteArray.append(applicationFileOwner);

            // manage download segment :
            this->downloadSegmentFromServer();
            break;
        }   

    case NoSuchArticleMessageId: case NoSuchArticleNumber: {
            this->postDownloadProcess(NotPresent);
            break;
        }

    case IdleTimeout: case QuitFromServer: {
            this->setConnectedClientStatus(ClientIdle);
            break;
        }

    default: {
            kDebug() << "Answer from host : " << answer << " not handled !" << "group :" << parent->getServerGroup()->getServerGroupId();
            // response not handled, consider that segment is not present :
            this->postDownloadProcess(NotPresent);
            break;
        }


    }

}



void NntpClient::downloadSegmentFromServer(){

    // answer received before time-out : OK
    serverAnswerTimer->stop();

    // read available data:
    QByteArray chunckData = tcpSocket->readAll();
    this->segmentByteArray.append(chunckData);

    // send size of downloaded data to the status bar :
    emit speedSignal(chunckData.size());

    // if end of download has been reached :
    if (this->segmentByteArray.endsWith("\r\n.\r\n")) {

        this->postDownloadProcess(Present);

    }
    // data are pending, next readyRead signal is expected before time-out :
    else {
        serverAnswerTimer->start();
    }

}



void NntpClient::postDownloadProcess(const UtilityNamespace::Article articlePresence){

    if (!this->segmentProcessed) {

        if (serverAnswerTimer->isActive()) {
            serverAnswerTimer->stop();
        }

        // consider that data will be correctly saved by default :
        bool isSaved = true;

        // save data only if article has been found on server :
        if (articlePresence == Present) {

            // replace new lines starting with double periods by a simple one (RFC 977)
            this->segmentByteArray.replace("\r\n..", "\r\n.");

            // save segment :
            QString temporaryFolder = Settings::temporaryFolder().path() + '/';
            isSaved = Utility::saveData(temporaryFolder, this->currentSegmentData.getPart(), this->segmentByteArray);

            // if file can not be saved, inform the user and stop downloads :
            if (!isSaved) {

                // segment has been downloaded but not saved, proceed to roll back :
                this->segmentDataRollBack();
                // send save error notification :
                emit saveFileErrorSignal(DuringDownload);

                // set client in Idle status for the moment :
                this->setConnectedClientStatus(ClientIdle);

            }
        }

        // article has not been found, try to download it with backup servers :
        else if (articlePresence == NotPresent) {

            // check if a backup server is available to download current segmentData
            bool nextServerFound = this->downloadSegmentWithBackupServer();

            // segment will be processed by another server :
            if (nextServerFound) {

                this->requestNewSegment();

                isSaved = false;
            }

        }

        // if article has been saved, set status to DownloadFinishStatus and update segment
        if (isSaved) {
            this->notifyDownloadHasFinished(articlePresence);
            this->requestNewSegment();
        }


    }

}


void NntpClient::requestNewSegment() {

    if (this->postingOk) {

        // set client ready for receiving next segment :
        this->setConnectedClientStatus(ClientSegmentRequest);

        // request a new segment :
        emit getNextSegmentSignal(parent);
    }
}



void NntpClient::downloadNextSegment(const SegmentData currentSegmentData) {

    this->segmentDataRollBack();

    this->currentSegmentData = currentSegmentData;

    // a new segment has arrived, indicate that this current segment has not been proceeded yet :
    this->segmentProcessed = false;

    // should not occur but due to asynchronous call, check that the
    // socket is in connected state before further processing :
    if ((tcpSocket->state() == QAbstractSocket::ConnectedState) ) {
        // get body message from server :
        this->sendBodyCommandToServer();
        
    }
    else {       

        this->postProcessIfBackupServer(DoNotRequestNewSegment);
        this->segmentDataRollBack();
    }

}


void NntpClient::noSegmentAvailable() {
    this->setConnectedClientStatus(ClientIdle);
}



void NntpClient::setConnectedClientStatus(const NntpClientStatus status, const TimerJob timerJob) {

    // update client status :
    this->clientStatus = status;

    if (this->clientStatus != ClientDownload &&
        this->currentSegmentData.isInitialized()) {

        this->postProcessIfBackupServer(DoNotRequestNewSegment);
        this->segmentDataRollBack();

    }


    // start/stop timers according to where this method has been called :
    if (timerJob == StartStopTimers) {

        // start disconnect timeout if idle :
        if (this->clientStatus == ClientIdle) {
            idleTimeOutTimer->start();
        }
        else {
            // client is connected and working, stop timers :
            if (idleTimeOutTimer->isActive()) {
                idleTimeOutTimer->stop();
            }
            if (tryToReconnectTimer->isActive()) {
                tryToReconnectTimer->stop();
            }
        }
    }
}



void NntpClient::segmentDataRollBack() {

    // in case of download errors it can happen that segmentDataRollBack()
    // is called several times with the same segmentData instance
    // check that roll back occurs only once per segmentData :
    if (!this->segmentProcessed) {

        if (this->currentSegmentData.getStatus() == DownloadStatus) {

            kDebug() << "segmentData roll back effective !";
            this->currentSegmentData.setStatus(IdleStatus);
            this->currentSegmentData.setProgress(PROGRESS_INIT);

            // update segment data status :
            emit updateDownloadSegmentSignal(this->currentSegmentData);

            this->segmentProcessed = true;
        }
    }

}


void NntpClient::postProcessIfBackupServer(NewSegmentRequest newSegmentRequest) {

    if (!this->segmentProcessed) {

        // this can occur with a backup server with incorrect settings or access denied,
        // skip the current segment and try to download with another backup server if any :
        if (!this->parent->isMasterServer()) {

            bool nextServerFound = this->downloadSegmentWithBackupServer();

            if (!nextServerFound) {

                // consider current segment has not found as there no other available bakcup server :
                this->notifyDownloadHasFinished(NotPresent);
            }

            // request new segment only in case this method call does not come from
            // disconnectedSlot() or errorSlot() as next segment request must be done after connection process :
            if (newSegmentRequest == RequestNewSegment) {
                this->requestNewSegment();
            }
        }

    }
}


bool NntpClient::downloadSegmentWithBackupServer() {

    bool nextServerFound = false;

    // get server group of the next backup server :
    int nextServerGroupId = this->parent->getServerGroup()->getServerManager()->getNextTargetServer(parent->getServerGroup()->getServerGroupId());

    if (nextServerGroupId != UtilityNamespace::NoTargetServer) {

        this->currentSegmentData.setReadyForNewServer(nextServerGroupId);

        // update segmentData :
        emit updateDownloadSegmentSignal(this->currentSegmentData);

        // tells clients connected to next server that this segment is pending :
        this->parent->getServerGroup()->getServerManager()->tryDownloadWithServer(nextServerGroupId);

        this->segmentProcessed = true;
        nextServerFound = true;

    }

    return nextServerFound;
}


void NntpClient::notifyDownloadHasFinished(const UtilityNamespace::Article articlePresence) {

    this->currentSegmentData.setDownloadFinished(articlePresence);

    this->segmentProcessed = true;

    // update segmentData :
    emit updateDownloadSegmentSignal(currentSegmentData);

}





bool NntpClient::isClientReady() {

    bool clientReady = true;

    if (this->authenticationDenied) {
        clientReady = false;
    }

    if (this->tcpSocket->state() == QAbstractSocket::ConnectedState &&
        !this->postingOk) {
        clientReady = false;
    }

    if (!clientReady) {
        this->setConnectedClientStatus(ClientIdle, DoNotTouchTimers);
    }

    return clientReady;
}



void NntpClient::disconnectRequestByManager() {

    // stop all timers :
    idleTimeOutTimer->stop();
    tryToReconnectTimer->stop();
    serverAnswerTimer->stop();

    this->authenticationDenied = false;
    this->postingOk = false;

    this->segmentDataRollBack();
    this->sendQuitCommandToServer();
    tcpSocket->abort();
}

void NntpClient::connectRequestByManager() {
    this->dataHasArrivedSlot();
}


//============================================================================================================//
//                                               SLOTS                                                        //
//============================================================================================================//

void NntpClient::readyReadSlot(){

    switch (this->clientStatus) {

    case ClientIdle: case ClientSegmentRequest: {

            this->getAnswerFromServer();
            break;
        }

    case ClientDownload: {
            this->downloadSegmentFromServer();
            break;
        }
    }

}



void NntpClient::dataHasArrivedSlot() {

    // try to connect if disconnected from server :
    if (tcpSocket->state() == QAbstractSocket::UnconnectedState) {

        this->connectToHost();
    }

    // if the client is not currently downloading, request another segment
    // only if socket is in connected state :
    if ( (this->clientStatus == ClientIdle) &&
         (tcpSocket->state() == QAbstractSocket::ConnectedState) ) {

        this->requestNewSegment();

    }

}



void NntpClient::connectedSlot(){

    emit connectionStatusSignal(Connected);

    // if reconnection succeeded by timer :
    if (tryToReconnectTimer->isActive()) {
        tryToReconnectTimer->stop();
    }

    // fetch new item :
    dataHasArrivedSlot();
}


void NntpClient::disconnectedSlot(){

    this->postingOk = false;

    this->setConnectedClientStatus(ClientIdle, DoNotTouchTimers);

    // if disconnection comes after an socket error, notify type of error in status bar :
    // (if disconnection is normal, nntpError = NoError) :
    emit nntpErrorSignal(nntpError);
    emit connectionStatusSignal(Disconnected);


    // try to reconnect only if login and password are ok :
    if (!this->authenticationDenied) {

        if (idleTimeOutTimer->isActive()) {
            idleTimeOutTimer->stop();
        }
        else if (!tryToReconnectTimer->isActive()) {
            tryToReconnectTimer->start();
        }
    }

}


void NntpClient::errorSlot(QAbstractSocket::SocketError socketError){

    kDebug() << this->parent->getServerGroup()->getServerGroupId() << socketError;

    this->setConnectedClientStatus(ClientIdle, DoNotTouchTimers);


    if (socketError == QAbstractSocket::HostNotFoundError){
        // connection failed, notify error now :
        emit nntpErrorSignal(HostNotFound);
    }

    if (socketError == QAbstractSocket::ConnectionRefusedError){
        // connection failed, notify error now :
        emit nntpErrorSignal(ConnectionRefused);
    }

    if (socketError == QAbstractSocket::RemoteHostClosedError){
        // disconnection will occur after this slot, notify error only when disconnect occurs :
        nntpError = RemoteHostClosed;
    }

    if (socketError == QAbstractSocket::SslHandshakeFailedError){
        // disconnection will occur after this slot, notify error only when disconnect occurs :
        nntpError = SslHandshakeFailed;
    }

}



void NntpClient::idleTimeOutSlot() {

    this->sendQuitCommandToServer();
    tcpSocket->disconnectFromHost();

}



void NntpClient::answerTimeOutSlot(){

    //kDebug() << "Host answer time out, reconnecting...";
    serverAnswerTimer->stop();

    this->setConnectedClientStatus(ClientIdle);

    // anticipate socket error notification -> reconnect immediately :
    this->sendQuitCommandToServer();
    tcpSocket->abort();

    kDebug() << this->parent->getServerGroup()->getServerGroupId();

    this->dataHasArrivedSlot();
}




void NntpClient::tryToReconnectSlot(){

    // try to connect, be sure to be unconnected before :
    if (tcpSocket->state() == QAbstractSocket::UnconnectedState) {
        this->connectToHost();
    }

}


void NntpClient::socketEncryptedSlot(){

    QString issuerOrgranisation = "Unknown";

    // retrieve peer certificate :
    QSslCertificate sslCertificate = tcpSocket->peerCertificate();

    // get issuer organization in order to display it as tooltip in status bar :
    if (!sslCertificate.isNull()) {
        issuerOrgranisation = sslCertificate.issuerInfo(QSslCertificate::Organization);
    }


    // retrieve errors occured during ssl handshake :
    QStringList sslErrors;

    foreach (QSslError currentSslError, this->tcpSocket->sslErrors()) {
        sslErrors.append(currentSslError.errorString());
    }

    // SSL connection is active, send also encryption method used by host :
    emit encryptionStatusSignal(true, tcpSocket->sessionCipher().encryptionMethod(), this->certificateVerified, issuerOrgranisation, sslErrors);

}



void NntpClient::peerVerifyErrorSlot() {

    // error occured during certificate verifying, set verify mode to QueryPeer in order to establish connection
    // but inform the user that certificate is not verified by tooltip in status bar :
    tcpSocket->setPeerVerifyMode(QSslSocket::QueryPeer);

    this->certificateVerified = false;

}




//============================================================================================================//
//                                            host commands                                                   //
//============================================================================================================//


void NntpClient::sendBodyCommandToServer(){
    QString commandStr("BODY <" + currentSegmentData.getPart() + ">\r\n");
    tcpSocket->write( commandStr.toLatin1(), commandStr.size());

    serverAnswerTimer->start();
}

void NntpClient::sendQuitCommandToServer(){
    QString commandStr("QUIT\r\n");
    tcpSocket->write( commandStr.toLatin1(), commandStr.size());
}

void NntpClient::sendUserCommandToServer(){
    QString commandStr("AUTHINFO USER " + parent->getServerData().getLogin() + "\r\n");
    tcpSocket->write( commandStr.toLatin1(), commandStr.size());
}

void NntpClient::sendPasswordCommandToServer(){
    QString commandStr("AUTHINFO PASS " + parent->getServerData().getPassword() + "\r\n");
    tcpSocket->write( commandStr.toLatin1(), commandStr.size());
}



