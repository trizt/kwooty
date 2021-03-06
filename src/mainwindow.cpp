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

#include "mainwindow.h"

#include <KApplication>
#include <QAction>
#include <KLocalizedString>
#include <KActionCollection>
#include <KStandardAction>
#include <KConfigDialog>
#include <KPageDialog>
#include <KPageWidgetItem>
#include <KMessageBox>
#include <KSaveFile>
#include "kwooty_debug.h"
#include <QMenuBar>

#include <QTextStream>
#include <QtGui>
#include <QDesktopWidget>
#include "kwootysettings.h"
#include "core.h"
#include "fileoperations.h"
#include "sidebar.h"
#include "actions/actionsmanager.h"
#include "actions/actionbuttonsmanager.h"
#include "actions/actionmergemanager.h"
#include "actions/actionrenamemanager.h"
#include "actions/actionfiledeletemanager.h"
#include "widgets/mystatusbar.h"
#include "widgets/mytreeview.h"
#include "widgets/centralwidget.h"
#include "widgets/sidebarwidget.h"
#include "shutdown/shutdownmanager.h"
#include "plugins/pluginmanager.h"
#include "preferences/kconfiggrouphandler.h"
#include "preferences/preferencesserver.h"
#include "preferences/preferencesgeneral.h"
#include "preferences/preferencesprograms.h"
#include "preferences/preferencesdisplay.h"
#include "preferences/preferencesshutdown.h"
#include "preferences/preferencesplugins.h"

#include "systray.h"

MainWindow::MainWindow(QWidget *parent): KXmlGuiWindow(parent)
{

    this->initVariables();

    // setup kconfig group handler :
    this->mKConfigGroupHandler = new KConfigGroupHandler(this);

    // setup central widget :
    this->mCentralWidget = new CentralWidget(this);

    // setup treeview :
    this->mTreeView = new MyTreeView(this);

    // setup core :
    this->mCore = new Core(this);

    // finish treeView init :
    this->mTreeView->achieveInit();

    // setup side bar manager :
    this->mSideBar = new SideBar(this);

    // create the user interface :
    QWidget *widget = new QWidget(this);
    this->buildLayout(widget);
    this->setCentralWidget(widget);

    this->setupActions();

    // setup statusBar :
    this->mStatusBar = new MyStatusBar(this);
    this->setStatusBar(this->mStatusBar);

    // setup system tray :
    this->systraySlot();

    // setup plugin manager :
    this->mPluginManager = new PluginManager(this);
    this->mPluginManager->loadPlugins();

    this->mQuitSelected = false;

    // hide main window when session is restored and systray icon is checked, else show main window :
    if (!(kapp->isSessionRestored() && Settings::sysTray()) ||
            (kapp->isSessionRestored() && !this->mKConfigGroupHandler->readMainWindowHiddenOnExit())) {

        this->show();
    }

    // notify that initialization has been fully performed :
    emit startupCompleteSignal();

}

MainWindow::~MainWindow()
{

}

void MainWindow::initVariables()
{
    this->mCore = 0;
    this->mCentralWidget = 0;
    this->mTreeView = 0;
    this->mSideBar = 0;
}

void MainWindow::buildLayout(QWidget *widget)
{

    QVBoxLayout *mainVBoxLayout = new QVBoxLayout(widget);
    mainVBoxLayout->setSpacing(2);
    mainVBoxLayout->setMargin(1);

    mainVBoxLayout->addWidget(this->mTreeView);
    mainVBoxLayout->addWidget(this->mSideBar->getSideBarWidget());

}

MyStatusBar *MainWindow::getStatusBar() const
{
    Q_ASSERT(this->mStatusBar != 0);
    return this->mStatusBar;
}

SideBar *MainWindow::getSideBar() const
{
    Q_ASSERT(this->mSideBar != 0);
    return this->mSideBar;
}

CentralWidget *MainWindow::getCentralWidget() const
{
    Q_ASSERT(this->mCentralWidget != 0);
    return this->mCentralWidget;
}

Core *MainWindow::getCore() const
{
    Q_ASSERT(this->mCore != 0);
    return this->mCore;
}

MyTreeView *MainWindow::getTreeView() const
{
    Q_ASSERT(this->mTreeView != 0);
    return this->mTreeView;
}

void MainWindow::setupActions()
{

    ActionsManager *actionsManager = this->mCore->getActionsManager();
    ActionButtonsManager *actionButtonsManager = actionsManager->getActionButtonsManager();

    //-----------------
    // custom Actions :
    //-----------------

    // clearAction :
    QAction *clearAction = new QAction(this);
    clearAction->setText(i18n("Clear"));
    clearAction->setIcon(QIcon::fromTheme("edit-clear-list"));
    clearAction->setToolTip(i18n("Remove all rows"));
    actionCollection()->setDefaultShortcut(clearAction, Qt::CTRL + Qt::Key_W);
    actionCollection()->addAction("clear", clearAction);
    connect(clearAction, SIGNAL(triggered(bool)), actionsManager, SLOT(clearSlot()));

    // startDownloadAction :
    QAction *startDownloadAction = new QAction(this);
    startDownloadAction->setText(i18n("Start"));
    startDownloadAction->setIcon(QIcon::fromTheme("media-playback-start"));
    startDownloadAction->setToolTip(i18n("Start download of selected rows"));
    actionCollection()->setDefaultShortcut(startDownloadAction, Qt::CTRL + Qt::Key_S);
    startDownloadAction->setEnabled(false);
    actionCollection()->addAction("start", startDownloadAction);
    connect(startDownloadAction, SIGNAL(triggered(bool)), actionsManager, SLOT(startDownloadSlot()));
    connect(actionButtonsManager, SIGNAL(setStartButtonEnabledSignal(bool)), startDownloadAction, SLOT(setEnabled(bool)));

    // pauseDownloadAction :
    QAction *pauseDownloadAction = new QAction(this);
    pauseDownloadAction->setText(i18n("Pause"));
    pauseDownloadAction->setIcon(QIcon::fromTheme("media-playback-pause"));
    pauseDownloadAction->setToolTip(i18n("Pause download of selected rows"));
    actionCollection()->setDefaultShortcut(pauseDownloadAction, Qt::CTRL + Qt::Key_P);
    pauseDownloadAction->setEnabled(false);
    actionCollection()->addAction("pause", pauseDownloadAction);
    connect(pauseDownloadAction, SIGNAL(triggered(bool)), actionsManager, SLOT(pauseDownloadSlot()));
    connect(actionButtonsManager, SIGNAL(setPauseButtonEnabledSignal(bool)), pauseDownloadAction, SLOT(setEnabled(bool)));

    // removeItemAction :
    QAction *removeItemAction = new QAction(this);
    removeItemAction->setText(i18n("Remove"));
    removeItemAction->setIcon(QIcon::fromTheme("list-remove"));
    removeItemAction->setToolTip(i18n("Remove all selected rows"));
    actionCollection()->setDefaultShortcut(removeItemAction, Qt::Key_Delete);
    removeItemAction->setEnabled(false);
    actionCollection()->addAction("remove", removeItemAction);
    connect(removeItemAction, SIGNAL(triggered(bool)), actionsManager, SLOT(removeRowSlot()));
    connect(actionButtonsManager, SIGNAL(setMoveButtonEnabledSignal(bool)), removeItemAction, SLOT(setEnabled(bool)));
    connect(actionButtonsManager, SIGNAL(setRemoveButtonEnabledSignal(bool)), removeItemAction, SLOT(setEnabled(bool)));

    // removeItemDeleteFileAction :
    QAction *removeItemDeleteFileAction = new QAction(this);
    removeItemDeleteFileAction->setText(i18n("Remove data"));
    removeItemDeleteFileAction->setIcon(QIcon::fromTheme("edit-delete"));
    removeItemDeleteFileAction->setToolTip(i18n("Remove all selected rows and respective downloaded contents"));
    actionCollection()->setDefaultShortcut(removeItemDeleteFileAction, Qt::SHIFT | Qt::Key_Delete);
    removeItemDeleteFileAction->setEnabled(false);
    actionCollection()->addAction("removeItemDeleteFile", removeItemDeleteFileAction);
    connect(removeItemDeleteFileAction, SIGNAL(triggered(bool)), actionsManager->getActionFileDeleteManager(), SLOT(actionTriggeredSlot()));
    connect(actionButtonsManager, SIGNAL(setMoveButtonEnabledSignal(bool)), removeItemDeleteFileAction, SLOT(setEnabled(bool)));
    connect(actionButtonsManager, SIGNAL(setRemoveDeleteFileButtonEnabledSignal(bool)), removeItemDeleteFileAction, SLOT(setEnabled(bool)));

    // moveUpAction :
    QAction *moveUpAction = new QAction(this);
    moveUpAction->setText(i18n("Up"));
    moveUpAction->setIcon(QIcon::fromTheme("go-up"));
    moveUpAction->setToolTip(i18n("Go up all selected rows"));
    actionCollection()->setDefaultShortcut(moveUpAction, Qt::CTRL + Qt::Key_Up);
    moveUpAction->setEnabled(false);
    actionCollection()->addAction("moveUp", moveUpAction);
    connect(moveUpAction, SIGNAL(triggered(bool)), actionsManager, SLOT(moveUpSlot()));
    connect(actionButtonsManager, SIGNAL(setMoveButtonEnabledSignal(bool)), moveUpAction, SLOT(setEnabled(bool)));

    // moveToTopAction :
    QAction *moveToTopAction = new QAction(this);
    moveToTopAction->setText(i18n("Top"));
    moveToTopAction->setIcon(QIcon::fromTheme("go-top"));
    moveToTopAction->setToolTip(i18n("Move all selected rows to the top of the list"));
    actionCollection()->setDefaultShortcut(moveToTopAction, Qt::CTRL + Qt::Key_PageUp);
    moveToTopAction->setEnabled(false);
    actionCollection()->addAction("moveTop", moveToTopAction);
    connect(moveToTopAction, SIGNAL(triggered(bool)), actionsManager, SLOT(moveToTopSlot()));
    connect(actionButtonsManager, SIGNAL(setMoveButtonEnabledSignal(bool)), moveToTopAction, SLOT(setEnabled(bool)));

    // moveDownAction :
    QAction *moveDownAction = new QAction(this);
    moveDownAction->setText(i18n("Down"));
    moveDownAction->setIcon(QIcon::fromTheme("go-down"));
    moveDownAction->setToolTip(i18n("Go down all selected rows"));
    actionCollection()->setDefaultShortcut(moveDownAction, Qt::CTRL + Qt::Key_Down);
    moveDownAction->setEnabled(false);
    actionCollection()->addAction("moveDown", moveDownAction);
    connect(moveDownAction, SIGNAL(triggered(bool)), actionsManager, SLOT(moveDownSlot()));
    connect(actionButtonsManager, SIGNAL(setMoveButtonEnabledSignal(bool)), moveDownAction, SLOT(setEnabled(bool)));

    // moveToBottomAction :
    QAction *moveToBottomAction = new QAction(this);
    moveToBottomAction->setText(i18n("Bottom"));
    moveToBottomAction->setIcon(QIcon::fromTheme("go-bottom"));
    moveToBottomAction->setToolTip(i18n("Move all selected rows to the bottom of the list"));
    actionCollection()->setDefaultShortcut(moveToBottomAction, Qt::CTRL + Qt::Key_PageDown);
    moveToBottomAction->setEnabled(false);
    actionCollection()->addAction("moveBottom", moveToBottomAction);
    connect(moveToBottomAction, SIGNAL(triggered(bool)), actionsManager, SLOT(moveToBottomSlot()));
    connect(actionButtonsManager, SIGNAL(setMoveButtonEnabledSignal(bool)), moveToBottomAction, SLOT(setEnabled(bool)));

    // openFolderAction :
    QAction *openFolderAction = new QAction(this);
    openFolderAction->setText(i18n("Downloads"));
    openFolderAction->setIcon(QIcon::fromTheme("folder-downloads"));
    openFolderAction->setToolTip(i18n("Open current download folder"));
    actionCollection()->setDefaultShortcut(openFolderAction, Qt::CTRL + Qt::Key_D);
    openFolderAction->setEnabled(true);
    actionCollection()->addAction("downloadFolder", openFolderAction);
    connect(openFolderAction, SIGNAL(triggered(bool)), actionsManager, SLOT(openFolderSlot()));

    // shutdownAction :
    QAction *shutdownAction = new QAction(this);
    shutdownAction->setText(i18n("Shutdown"));
    shutdownAction->setIcon(QIcon::fromTheme("system-shutdown"));
    shutdownAction->setToolTip(i18n("Schedule system shutdown"));
    actionCollection()->setDefaultShortcut(shutdownAction, Qt::CTRL + Qt::Key_T);
    shutdownAction->setEnabled(false);
    shutdownAction->setCheckable(true);
    actionCollection()->addAction("shutdown", shutdownAction);
    connect(shutdownAction, SIGNAL(triggered(bool)), mCore->getShutdownManager(), SLOT(enableSystemShutdownSlot(bool)));
    connect(mCore->getShutdownManager(), SIGNAL(setShutdownButtonCheckedSignal(bool)), shutdownAction, SLOT(setChecked(bool)));
    connect(mCore->getShutdownManager(), SIGNAL(setShutdownButtonEnabledSignal(bool)), shutdownAction, SLOT(setEnabled(bool)));

    // startAllDownloadAction :
    QAction *startAllDownloadAction = new QAction(this);
    startAllDownloadAction->setText(i18n("Start all"));
    startAllDownloadAction->setIcon(QIcon::fromTheme("media-playback-start"));
    startAllDownloadAction->setToolTip(i18n("Start all paused downloads"));
    startAllDownloadAction->setEnabled(false);
    actionCollection()->addAction("startAll", startAllDownloadAction);
    connect(startAllDownloadAction, SIGNAL(triggered(bool)), actionsManager, SLOT(startAllDownloadSlot()));
    connect(actionButtonsManager, SIGNAL(setStartAllButtonEnabledSignal(bool)), startAllDownloadAction, SLOT(setEnabled(bool)));

    // pauseAllDownloadAction :
    QAction *pauseAllDownloadAction = new QAction(this);
    pauseAllDownloadAction->setText(i18n("Pause all"));
    pauseAllDownloadAction->setIcon(QIcon::fromTheme("media-playback-pause"));
    pauseAllDownloadAction->setToolTip(i18n("Pause all pending downloads"));
    pauseAllDownloadAction->setEnabled(false);
    actionCollection()->addAction("pauseAll", pauseAllDownloadAction);
    connect(pauseAllDownloadAction, SIGNAL(triggered(bool)), actionsManager, SLOT(pauseAllDownloadSlot()));
    connect(actionButtonsManager, SIGNAL(setPauseAllButtonEnabledSignal(bool)), pauseAllDownloadAction, SLOT(setEnabled(bool)));

    // retryDownloadAction :
    QAction *retryDownloadAction = new QAction(this);
    retryDownloadAction->setText(i18n("Retry"));
    retryDownloadAction->setIcon(QIcon::fromTheme("edit-redo"));
    retryDownloadAction->setToolTip(i18n("Retry to download selected rows"));
    actionCollection()->setDefaultShortcut(retryDownloadAction, Qt::CTRL + Qt::Key_R);
    retryDownloadAction->setEnabled(false);
    actionCollection()->addAction("retryDownload", retryDownloadAction);
    connect(retryDownloadAction, SIGNAL(triggered(bool)), actionsManager, SLOT(retryDownloadSlot()));
    connect(actionButtonsManager, SIGNAL(setRetryButtonEnabledSignal(bool)), retryDownloadAction, SLOT(setEnabled(bool)));

    // manualExtractAction :
    QAction *manualExtractAction = new QAction(this);
    manualExtractAction->setText(i18n("Repair and extract"));
    manualExtractAction->setIcon(QIcon::fromTheme("archive-extract"));
    manualExtractAction->setToolTip(i18n("Manually verify and extract selected item"));
    actionCollection()->setDefaultShortcut(manualExtractAction, Qt::CTRL + Qt::Key_E);
    manualExtractAction->setEnabled(false);
    actionCollection()->addAction("manualExtract", manualExtractAction);
    connect(manualExtractAction, SIGNAL(triggered(bool)), actionsManager, SLOT(manualExtractSlot()));
    connect(actionButtonsManager, SIGNAL(setManualExtractActionSignal(bool)), manualExtractAction, SLOT(setEnabled(bool)));

    // mergeNzbAction :
    QAction *mergeNzbAction = new QAction(this);
    mergeNzbAction->setText(i18n("Merge with..."));
    mergeNzbAction->setIcon(QIcon::fromTheme("mail-message-new"));
    mergeNzbAction->setToolTip(i18n("Merge nzb content into another nzb"));
    mergeNzbAction->setEnabled(false);
    actionCollection()->addAction("mergeNzb", mergeNzbAction);
    connect(actionButtonsManager, SIGNAL(setMergeNzbButtonEnabledSignal(bool)), mergeNzbAction, SLOT(setEnabled(bool)));

    // add a submenu that will be filled dynamically :
    QMenu *mergeSubMenu = new QMenu(this);
    mergeNzbAction->setMenu(mergeSubMenu);

    // prepare corresponding submenu :
    connect(mergeSubMenu, SIGNAL(aboutToShow()), actionsManager->getActionMergeManager(), SLOT(mergeSubMenuAboutToShowSlot()));

    // retrieve selected action from submenu :
    connect(mergeSubMenu, SIGNAL(triggered(QAction*)), actionsManager->getActionMergeManager(), SLOT(actionTriggeredSlot(QAction*)));

    // renameNzbAction :
    QAction *renameNzbAction = new QAction(this);
    renameNzbAction->setText(i18n("Rename..."));
    renameNzbAction->setIcon(QIcon::fromTheme("edit-rename"));
    renameNzbAction->setToolTip(i18n("Rename nzb and its corresponding download folder"));
    actionCollection()->setDefaultShortcut(renameNzbAction, Qt::Key_F2);
    renameNzbAction->setEnabled(false);
    actionCollection()->addAction("renameNzb", renameNzbAction);
    connect(renameNzbAction, SIGNAL(triggered(bool)), actionsManager->getActionRenameManager(), SLOT(actionTriggeredSlot()));
    connect(actionButtonsManager, SIGNAL(setRenameNzbButtonEnabledSignal(bool)), renameNzbAction, SLOT(setEnabled(bool)));

    //-------------------
    // standard Actions :
    //-------------------

    // quit action :
    KStandardAction::quit(this, SLOT(quit()), actionCollection());

    // open action :
    KStandardAction::open(this, SLOT(openFile()), actionCollection());

    // settings action :
    KStandardAction::preferences(this, SLOT(showSettings()), actionCollection());

    // shown menuBar action :
    KStandardAction::showMenubar(this, SLOT(toggleShowMenuBar()), actionCollection());

    setupGUI();

}

QAction *MainWindow::getActionFromName(const QString &actionName)
{

    return actionCollection()->action(actionName);

}

void MainWindow::showSettings(UtilityNamespace::PreferencesPage preferencesPage)
{

    // if instance has already been created :
    if (KConfigDialog::exists("settings")) {

        emit aboutToShowSettingsSignal();

        if (this->mPreferencesPagesMap.contains(preferencesPage)) {

            KConfigDialog::exists("settings")->setCurrentPage(this->mPreferencesPagesMap.value(preferencesPage));
        }

        KConfigDialog::showDialog("settings");

    } else {

        // dialog instance is not et created, create it :
        KConfigDialog *dialog = new KConfigDialog(this, "settings", Settings::self());

        PreferencesGeneral *preferencesGeneral = new PreferencesGeneral();
        KPageWidgetItem *preferencesGeneralPage = dialog->addPage(preferencesGeneral, i18n("General"), "preferences-system", i18n("General Setup"));
        this->mPreferencesPagesMap.insert(GeneralPage, preferencesGeneralPage);

        PreferencesServer *preferencesServer = new PreferencesServer(dialog);
        KPageWidgetItem *preferencesServerPage = dialog->addPage(preferencesServer, i18n("Connection"), "network-workgroup", i18n("Setup Server Connection"));
        this->mPreferencesPagesMap.insert(ServerPage, preferencesServerPage);

        PreferencesPrograms *preferencesPrograms = new PreferencesPrograms();
        KPageWidgetItem *preferencesProgramsPage = dialog->addPage(preferencesPrograms, i18n("Programs"), "system-run", i18n("Setup External Programs"));
        this->mPreferencesPagesMap.insert(ProgramsPage, preferencesProgramsPage);

        PreferencesDisplay *preferencesDisplay = new PreferencesDisplay();
        KPageWidgetItem *preferencesDisplayPage = dialog->addPage(preferencesDisplay, i18n("Display modes"), "view-choose", i18n("Setup Display Modes"));
        this->mPreferencesPagesMap.insert(DisplayPage, preferencesDisplayPage);

        PreferencesShutdown *preferencesShutdown = new PreferencesShutdown(this->mCore);
        KPageWidgetItem *preferencesShutdownPage = dialog->addPage(preferencesShutdown, i18n("Shutdown"), "system-shutdown", i18n("Setup System Shutdown"));
        this->mPreferencesPagesMap.insert(ShutdownPage, preferencesShutdownPage);

        PreferencesPlugins *preferencesPlugins = new PreferencesPlugins(dialog, this->mPluginManager);
        KPageWidgetItem *preferencesPluginsPage = dialog->addPage(preferencesPlugins, i18n("Plugins"), "preferences-plugin", i18n("Plugins Setup"));
        this->mPreferencesPagesMap.insert(PluginsPage, preferencesPluginsPage);

        connect(dialog, SIGNAL(settingsChanged(QString)), this->mCore, SLOT(updateSettingsSlot()));
        connect(dialog, SIGNAL(settingsChanged(QString)), this->mKConfigGroupHandler, SLOT(settingsChangedSlot()));
        connect(dialog, SIGNAL(settingsChanged(QString)), preferencesPrograms, SLOT(aboutToShowSettingsSlot()));
        connect(dialog, SIGNAL(settingsChanged(QString)), this, SLOT(systraySlot()));
        connect(this, SIGNAL(aboutToShowSettingsSignal()), preferencesPrograms, SLOT(aboutToShowSettingsSlot()));

        // show settings box :
        this->showSettings(preferencesPage);

    }

}

QSize MainWindow::sizeHint() const
{
    return QSize(QApplication::desktop()->screenGeometry(this).width() / 1.5,
                 QApplication::desktop()->screenGeometry(this).height() / 2);
}

//============================================================================================================//
//                                               SLOTS                                                        //
//============================================================================================================//

void MainWindow::toggleShowMenuBar()
{

    if (this->menuBar()->isVisible()) {
        this->menuBar()->hide();
    } else {
        this->menuBar()->show();
    }
}

void MainWindow::openFile()
{

    this->mCore->getFileOperations()->openFile();

}

void MainWindow::openFileWithFileMode(const KUrl &nzbUrl, UtilityNamespace::OpenFileMode openFileMode)
{

    this->mCore->getFileOperations()->openFileWithFileMode(nzbUrl, openFileMode);

}

void MainWindow::quit()
{

    // quit has been requested :
    this->mQuitSelected = true;

    // call queryclose() for quit confirmation :
    if (this->queryClose()) {
        kapp->quit();
    }

}

bool MainWindow::queryClose()
{

    // by default quit the application :
    bool confirmQuit = true;

    // session manager is not responsible from this call :
    if (!kapp->sessionSaving()) {

        // if the main window is just closed :
        if (!this->mQuitSelected) {

            // if system tray icon exists :
            if (Settings::sysTray()) {

                // display a warning message :
                KMessageBox::information(this,
                                         i18n("<qt>Closing the main window will keep Kwooty running in the System Tray. "
                                              "Use <B>Quit</B> from the menu or the Kwooty tray icon to exit the application.</qt>"),
                                         i18n("Docking in System Tray"), "hideOnCloseInfo");

                // hide the main window and don't quit :
                this->hide();
                confirmQuit = false;
            }
            // system tray icon does not exist, ask to save data and close the application :
            else {
                this->askForSavingDownloads(confirmQuit);
            }
        }
        // quit action has been performed, ask to save data and close the application :
        else {
            this->askForSavingDownloads(confirmQuit);
        }
    }
    // session manager is about to quit, just save data silently and quit :
    else {
        mCore->savePendingDownloads(UtilityNamespace::ShutdownMethodUnknown, SaveSilently);
    }

    return confirmQuit;

}

bool MainWindow::queryExit()
{

    this->mKConfigGroupHandler->writeMainWindowHiddenOnExit(this->isHidden());
    this->mSideBar->saveState();

    return true;
}

void MainWindow::askForSavingDownloads(bool &confirmQuit)
{

    int answer = mCore->savePendingDownloads();

    if (answer == KMessageBox::Cancel) {
        this->mQuitSelected = false;
        confirmQuit = false;
    }

}

void MainWindow::systraySlot()
{

    // remove system tray if requested by user :
    if (!Settings::sysTray() && this->mSysTray) {
        delete this->mSysTray;
    }
    // setup system tray if requested by user :
    else if (Settings::sysTray() && !this->mSysTray) {
        this->mSysTray = new SysTray(this);
    }

}

