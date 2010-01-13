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

#include "shutdownmanager.h"

#include <KDebug>
#include <KJob>
#include <KMessageBox>
#include <KProcess>
#include <KApplication>
#include <kworkspace/kworkspace.h>
#include <solid/powermanagement.h>
using namespace Solid::PowerManagement;
using namespace Solid::Control;

#include <QDateTime>

#include "centralwidget.h"
#include "itemparentupdater.h"
#include "mystatusbar.h"
#include "settings.h"


ShutdownManager::ShutdownManager(CentralWidget* parent) : QObject (parent) {
    
    // variable initialisation :
    this->parent = parent;
    this->noActivityCounter = 0;
    this->enableSystemShutdown = false;

    // system shutdown command for gnome desktop :
    this->gnomeShutdownApplication = "/usr/bin/gnome-session-save";

    // check if system has to shudown every 10 seconds :
    this->shutdownTimerInterval = 10000;

    // store settings values :
    this->storeSettings();

    // create activity timer :
    this->activityMonitorTimer = new QTimer(this);

    // create system shutdown timer :
    this->launchShutdownTimer = new QTimer(this);

    this->setupConnections();

}


void ShutdownManager::setupConnections() {

    // check application activity (download, extract, etc...) at each timeout :
    connect(this->activityMonitorTimer, SIGNAL(timeout()), this, SLOT(retrieveCurrentJobsInfoSlot()));

    // launch system shutdown after launchShutdownTimer timeout :
    connect(this->launchShutdownTimer, SIGNAL(timeout()), this, SLOT(launchSystemShutdownSlot()));

    // parent notify that settings have changed :
    connect (parent, SIGNAL(settingsChangedSignal()), this, SLOT(settingsChangedSlot()));

    // disable shutdown scheduler if user removed all rows :
    connect (parent->getTreeView(), SIGNAL(allRowRemovedSignal()), this, SLOT(shutdownCancelledSlot()));

    // enable or disable shutdown button according to nzb parent status:
    connect(parent->getItemParentUpdater(), SIGNAL(statusItemUpdatedSignal()), this, SLOT(statusItemUpdatedSlot()));

    // send shutdown info in status bar :
    connect(this, SIGNAL(statusBarShutdownInfoSignal(QString, QString)), parent->getStatusBar(), SLOT(statusBarShutdownInfoSlot(QString, QString)));

}



void ShutdownManager::systemAboutToShutdown() {
    
    // stop timer and reset noActivityCounter :
    this->enableSystemShutdownSlot(false);

    // shutdown system automatically in 10 seconds :
    this->launchShutdownTimer->start(10000);

    // get shutdown method text :
    UtilityNamespace::SystemShutdownType systemShutdownType = this->getChosenShutdownType();
    QString shutdownMethodText = this->getShutdownMethodText(systemShutdownType);

    // get button item in order to set text :
    KGuiItem continueGuiItem = KStandardGuiItem::cont();
    continueGuiItem.setText(shutdownMethodText);

    // finally display shutdown confirmation dialog :
    QString status = QString(i18n("Kwooty is about to %1 system. Continue?")).arg(shutdownMethodText.toLower());
    int answer = KMessageBox::warningContinueCancel(this->parent,
                                                    status,
                                                    i18n("Warning"),
                                                    continueGuiItem);

    // user has confirmed shutdown, launch shutdown right now :
    if (answer == KMessageBox::Continue) {
        this->launchSystemShutdownSlot();
    }
    // shutdown cancelled :
    else {
        // stop shutdown timer if cancelled bu the user :
        this->launchShutdownTimer->stop();

        // update shutdown button (not enabled/checked) :
        this->shutdownCancelledSlot();
    }
    
}



void ShutdownManager::storeSettings() {

    //settings have changed, store new values :
    this->jobsRadioButton = Settings::jobsRadioButton();
    this->timerRadioButton = Settings::timerRadioButton();
    this->pausedShutdown = Settings::pausedShutdown();
    this->scheduleDateTimeStr = Settings::scheduleDateTime().time().toString("hh:mm");
}


void ShutdownManager::updateStatusBar() {

    QString shutdownMethodText;
    QString shutdownMethodIcon;

    // if shutdown scheduler is active :
    if (this->enableSystemShutdown) {

        // define shutown method text :
        if (Settings::jobsRadioButton()) {
            shutdownMethodText = ' ' + i18n("when jobs complete");
        }

        if (Settings::timerRadioButton()) {
            // get scheduled shutdown time :
            QDateTime dateTime = QDateTime::currentDateTime();
            dateTime = dateTime.addSecs(Settings::scheduleDateTime().time().hour() * 3600 +
                                        Settings::scheduleDateTime().time().minute() * 60);

            shutdownMethodText = ' ' + i18n("at") + ' ' + dateTime.toString("hh:mm");
        }


        // define shutdown method icon :
        QMap<QString, QString>iconAvailableShutdownMap = this->retrieveIconAvailableShutdownMap();
        shutdownMethodIcon = iconAvailableShutdownMap.key(this->getShutdownMethodText(this->getChosenShutdownType()));

    }

    // send info to status bar :
    emit statusBarShutdownInfoSignal(shutdownMethodIcon ,shutdownMethodText);

}




void ShutdownManager::requestShutdown() {
    
    // check type of session and call proper shutdown method
    // if KDE session :
    if (this->retrieveSessionType() == ShutdownManager::Kde) {

        // halt the system now :
        bool success = KWorkSpace::requestShutDown(KWorkSpace::ShutdownConfirmNo,
                                                   KWorkSpace::ShutdownTypeHalt,
                                                   KWorkSpace::ShutdownModeForceNow);


        // if shutdown request has failed, inform the user :
        if (!success) {
            this->displayShutdownErrorMessageBox(i18n("Shutdown has failed (session manager can not be contacted)."));
        }
    }
    // if GNOME session :
    else if (this->retrieveSessionType() == ShutdownManager::Gnome) {

        // list of arguments for gnome-session-save command line :
        QStringList args;
        args.append("--shutdown-dialog");

        // halt the system now :
        KProcess* shutdowProcess = new KProcess(this);
        shutdowProcess->setProgram(this->gnomeShutdownApplication, args);
        shutdowProcess->start();
        shutdowProcess->closeWriteChannel();

    }

}



void ShutdownManager::requestSuspend(Solid::Control::PowerManager::SuspendMethod suspendMethod) {

    // requests a suspend of the system :
    bool success = Solid::Control::PowerManager::suspend(suspendMethod)->exec();

    if (success) {
        kapp->quit();
    }
    else {
        kDebug() << "shutdown job failed !";
    }
    
}


ShutdownManager::SessionType ShutdownManager::retrieveSessionType() {

    ShutdownManager::SessionType sessionType = ShutdownManager::Unknown;

    QString desktopSession;

    // check if session is a KDE session :
    desktopSession = ::getenv("KDE_FULL_SESSION");
    if (desktopSession.contains("true", Qt::CaseInsensitive)) {

        sessionType = ShutdownManager::Kde;
    }

    // check if session is a GNOME session :
    else {
        desktopSession = ::getenv("GNOME_DESKTOP_SESSION_ID");
        if (!desktopSession.isEmpty()) {

            if (QFile::exists(this->gnomeShutdownApplication)) {
                sessionType = ShutdownManager::Gnome;

            }
        }
    }

    //kDebug() << "desktopSession : " << desktopSession;

    // return kde or gnome desktop session :
    return sessionType;

}


UtilityNamespace::SystemShutdownType ShutdownManager::getChosenShutdownType() {

    UtilityNamespace::SystemShutdownType systemShutdownType = UtilityNamespace::ShutdownMethodUnknown;

    QList<UtilityNamespace::SystemShutdownType> indexShutdownTypeList = this->retrieveAvailableShutdownMethods();

    // ensure that list contains element :
    if (indexShutdownTypeList.size() > Settings::shutdownMethods()) {
        systemShutdownType = indexShutdownTypeList.at(Settings::shutdownMethods());
    }

    return systemShutdownType;

}




QList<UtilityNamespace::SystemShutdownType> ShutdownManager::retrieveAvailableShutdownMethods() {

    QList<UtilityNamespace::SystemShutdownType> indexShutdownTypeList;

    // at first add system shutdown if session has been identified as kde or gnome :
    if (this->retrieveSessionType() != ShutdownManager::Unknown) {
        indexShutdownTypeList.append(UtilityNamespace::Shutdown);
    }

    // then add supported sleep types by system :
    foreach (SleepState sleepState, Solid::PowerManagement::supportedSleepStates()) {

        // add standby :
        if (sleepState == StandbyState) {
            indexShutdownTypeList.append(UtilityNamespace::Standby);
        }
        // add suspend :
        if (sleepState == SuspendState) {
            indexShutdownTypeList.append(UtilityNamespace::Suspend);
        }
        // add hibernate :
        if (sleepState == HibernateState) {
            indexShutdownTypeList.append(UtilityNamespace::Hibernate);
        }
    }

    return indexShutdownTypeList;

}



QMap<QString, QString> ShutdownManager::retrieveIconAvailableShutdownMap() {

    QMap<QString, QString>iconAvailableShutdownMap;

    foreach (UtilityNamespace::SystemShutdownType shutdownType, this->retrieveAvailableShutdownMethods()) {

        // add shutdown :
        if (shutdownType == UtilityNamespace::Shutdown) {
            iconAvailableShutdownMap.insertMulti("system-shutdown", this->getShutdownMethodText(shutdownType));

        }
        // add standby :
        if (shutdownType == UtilityNamespace::Standby) {
            iconAvailableShutdownMap.insertMulti("system-suspend", this->getShutdownMethodText(shutdownType));

        }
        // add suspend :
        if (shutdownType == UtilityNamespace::Suspend) {
            iconAvailableShutdownMap.insertMulti("system-suspend", this->getShutdownMethodText(shutdownType));

        }
        // add hibernate :
        if (shutdownType == UtilityNamespace::Hibernate) {
            iconAvailableShutdownMap.insertMulti("system-suspend-hibernate", this->getShutdownMethodText(shutdownType));


        }
    }

    return iconAvailableShutdownMap;

}



QString ShutdownManager::getShutdownMethodText(UtilityNamespace::SystemShutdownType systemShutdownType) {

    QString shutdownText;

    // shutdown :
    if (systemShutdownType == UtilityNamespace::Shutdown) {
        shutdownText = i18n("Shutdown");

    }
    // standby :
    else if (systemShutdownType == UtilityNamespace::Standby) {
        shutdownText = i18n("Standby");

    }
    // suspend :
    else if (systemShutdownType == UtilityNamespace::Suspend) {
        shutdownText = i18n("Suspend to RAM");

    }
    // hibernate :
    else if (systemShutdownType == UtilityNamespace::Hibernate) {
        shutdownText = i18n("Suspend to disk");

    }

    return shutdownText;
}



void ShutdownManager::displayShutdownErrorMessageBox(const QString& message) {

    KMessageBox::messageBox(parent,
                            KMessageBox::Sorry,
                            message);

    // uncheck shutdown button :
    this->shutdownCancelledSlot();

}


//============================================================================================================//
//                                               SLOTS                                                        //
//============================================================================================================//

void ShutdownManager::settingsChangedSlot() {

    if ( this->jobsRadioButton != Settings::jobsRadioButton()   ||
         this->timerRadioButton != Settings::timerRadioButton() ||
         this->pausedShutdown != Settings::pausedShutdown()   ||
         this->scheduleDateTimeStr != Settings::scheduleDateTime().time().toString("hh:mm") ) {

        // update settings :
        this->storeSettings();

        // relaunch shutdown schedule according to new settings :
        this->enableSystemShutdownSlot(this->enableSystemShutdown);

    }

    this->updateStatusBar();

}


void ShutdownManager::shutdownCancelledSlot() {

    // shutdown cancelled, set system button as "not checked" :
    emit setShutdownButtonCheckedSignal(false);

    // if no more jobs are available, set system button as "not enabled" :
    if (this->parent->getTreeView()->areJobsFinished()) {
        emit setShutdownButtonEnabledSignal(false);
    }

    // reset variables :
    this->enableSystemShutdownSlot(false);

}

void ShutdownManager::statusItemUpdatedSlot() {

    // if activity detected, set shutdown button as enabled :
    if (!this->parent->getTreeView()->areJobsFinished()) {

        emit setShutdownButtonEnabledSignal(true);
    }
    // else if shutdown scheduler is not active, shutdown button should be disabled :
    else if (!this->enableSystemShutdown){
        emit setShutdownButtonEnabledSignal(false);
    }


}




void ShutdownManager::enableSystemShutdownSlot(bool enable) {

    this->enableSystemShutdown = enable;

    if (this->enableSystemShutdown) {

        // if shutdown when jobs are finished :
        if (Settings::jobsRadioButton()) {

            // start activity polling timer :
            if (!this->parent->getTreeView()->areJobsFinished()) {

                this->activityMonitorTimer->start(this->shutdownTimerInterval);

            }

        }

        // if shutdown at a given time :
        if (Settings::timerRadioButton()) {

            // set Timeout to time set in settings :
            this->activityMonitorTimer->stop();
            this->activityMonitorTimer->start(Settings::scheduleDateTime().time().hour() * UtilityNamespace::HOURS_TO_MILLISECONDS +
                                              Settings::scheduleDateTime().time().minute() * UtilityNamespace::MINUTES_TO_MILLISECONDS);


        }

    }
    // if scheduling is disabled, stop monitoring :
    else {

        this->activityMonitorTimer->stop();
        this->statusItemUpdatedSlot();
        this->noActivityCounter = 0;
    }


    this->updateStatusBar();


}



void ShutdownManager::retrieveCurrentJobsInfoSlot(){


    if (Settings::jobsRadioButton()) {

        bool jobsFinished = this->parent->getTreeView()->areJobsFinished();

        // set timer interval to lower value if jobs finished has been confirmed :
        if (jobsFinished) {

            if (this->activityMonitorTimer->interval() != 1000) {
                this->activityMonitorTimer->setInterval(1000);
            }

            this->noActivityCounter++;

            // if no more downloads / repairing / extracting processes are active 3 consecutive times, shutdown system :
            if (this->noActivityCounter == 3) {

                //display warning to user before shutdown :
                this->systemAboutToShutdown();

            }
        }
        // activity has started again, reset variables :
        else {
            this->activityMonitorTimer->setInterval(this->shutdownTimerInterval);
            this->noActivityCounter = 0;
        }
    }

    else if (Settings::timerRadioButton()) {

        // if it's time to shutdown :
        this->systemAboutToShutdown();
    }

}



void ShutdownManager::launchSystemShutdownSlot() {

    this->launchShutdownTimer->stop();

    // save potential pending data for future session restoring :
    parent->savePendingDownloads(true);

    // get type of system shutdown :
    switch (this->getChosenShutdownType()) {

    case UtilityNamespace::Shutdown: {
            this->requestShutdown();
            break;
        }

    case UtilityNamespace::Standby: {
            this->requestSuspend(Solid::Control::PowerManager::Standby);
            break;
        }

    case UtilityNamespace::Suspend: {
            this->requestSuspend(Solid::Control::PowerManager::ToRam);
            break;
        }

    case UtilityNamespace::Hibernate: {
            this->requestSuspend(Solid::Control::PowerManager::ToDisk);
            break;
        }

    default: {
            this->displayShutdownErrorMessageBox(i18n("System shutdown type unknown, shutdown is not possible!"));
            break;
        }

    }

}

