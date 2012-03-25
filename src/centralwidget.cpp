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

#include "centralwidget.h"

#include <KMessageBox>
#include <KPasswordDialog>
#include <KDebug>
#include <KIcon>
#include <QtGui>

#include "nzbfilehandler.h"
#include "utility.h"
#include "kwootysettings.h"
#include "clientmanagerconn.h"
#include "mytreeview.h"
#include "segmentmanager.h"
#include "segmentsdecoderthread.h"
#include "repairdecompressthread.h"
#include "standarditemmodel.h"
#include "standarditemmodelquery.h"
#include "itemparentupdater.h"
#include "itemchildrenmanager.h"
#include "datarestorer.h"
#include "shutdownmanager.h"
#include "fileoperations.h"
#include "mainwindow.h"
#include "notificationmanager.h"
#include "servermanager.h"
#include "observers/clientsobserver.h"
#include "observers/queuefileobserver.h"
#include "data/itemstatusdata.h"



CentralWidget::CentralWidget(MainWindow* parent) : QWidget(parent) {

    // init the downloadModel :
    downloadModel = new StandardItemModel(this);

    // query model according to items status :
    modelQuery = new StandardItemModelQuery(this);

    // init treeview :
    treeView = new MyTreeView(this);

    // init queue file observer :
    queueFileObserver = new QueueFileObserver(this);

    // collect connection statuses from clients and build stats info :
    clientsObserver = new ClientsObserver(this);

    // update view according to items data :
    itemParentUpdater = new ItemParentUpdater(this);

    // manage dispatching / updating segments related to one item :
    segmentManager = new SegmentManager(this);

    // save and restore pending downloads from previous session :
    dataRestorer = new DataRestorer(this);

    // setup segment decoder thread :
    segmentsDecoderThread = new SegmentsDecoderThread(this);

    // setup repairing and decompressing thread :
    repairDecompressThread = new RepairDecompressThread(this);

    // handle nntp clients with one or several servers :
    serverManager = new ServerManager(this);

    // set download and temp folders into home dir if not specified by user :
    this->initFoldersSettings();

    // setup shutdown manager :
    shutdownManager = new ShutdownManager(this);

    // setup nzb file opening closing :
    fileOperations = new FileOperations(this);

    // setup notification events :
    notificationManager = new NotificationManager(this);

    // init button code that avoid to display one message box per nntp client instance error :
    saveErrorButtonCode = 0;


}

CentralWidget::~CentralWidget() {

    delete this->segmentsDecoderThread;
    delete this->repairDecompressThread;

}



void CentralWidget::handleNzbFile(QFile& file, const QList<GlobalFileData>& inGlobalFileDataList) {

    // remove .nzb extension to file name:
    QFileInfo fileInfo(file.fileName());
    QString nzbName = fileInfo.completeBaseName();

    QList<GlobalFileData> globalFileDataList;

    // if list is empty it corresponds to a normal nzb file processing :
    if (inGlobalFileDataList.isEmpty()) {

        // parse the xml file and add elements to the model :
        NzbFileHandler nzbFileHandler;
        globalFileDataList = nzbFileHandler.processNzbFile(this, file, nzbName);

    }
    // else it corresponds to a data restoration from a previous session :
    else {

        globalFileDataList = inGlobalFileDataList;

    }


    // insert the data from file into the download model :
    if (!globalFileDataList.isEmpty()) {

        this->setDataToModel(globalFileDataList, nzbName);

        // update the status bar :
        this->statusBarFileSizeUpdate();

        // resize the column according to file's name length :
        int widthInPixel = treeView->fontMetrics().width(nzbName) + 100;

        // if column width is lower than current width, ajust it :
        if (treeView->columnWidth(FILE_NAME_COLUMN) < widthInPixel) {
            treeView->setColumnWidth(FILE_NAME_COLUMN, widthInPixel);
        }

        // notify nntp clients that data has arrived :
        emit dataHasArrivedSignal();
    }


}



int CentralWidget::savePendingDownloads(UtilityNamespace::SystemShutdownType systemShutdownType, const SaveFileBehavior saveFileBehavior) {

    int answer = dataRestorer->saveQueueData(saveFileBehavior);

    // disable dataRestorer when shutdown is requested because the app will be closed automatically
    // data saving will be triggered and then data saving dialog box could prevent system shutdown :
    if (systemShutdownType == UtilityNamespace::Shutdown) {
        dataRestorer->setActive(false);
    }

    return answer;
}


void CentralWidget::restoreDataFromPreviousSession(const QList<GlobalFileData>& globalFileDataList) {

    // instanciate a QFile to only get the name of the nzb needed by handleNzbFile();
    NzbFileData nzbFileData = globalFileDataList.at(0).getNzbFileData();
    QFile nzbFile(nzbFileData.getNzbName());

    // populate treeView with saved data :
    this->handleNzbFile(nzbFile, globalFileDataList);

    // update parent status to the same value as previous session :
    for (int i = 0; i < downloadModel->rowCount(); i++) {
        // retrieve nzb parent item :
        QStandardItem* parentFileNameItem = this->downloadModel->item(i, FILE_NAME_COLUMN);
        this->itemParentUpdater->updateNzbItems(parentFileNameItem->index());

    }

}



void CentralWidget::setDataToModel(const QList<GlobalFileData>& globalFileDataList, const QString& nzbName){

    QStandardItem* nzbNameItem = new QStandardItem(nzbName);
    QStandardItem* nzbStateItem = new QStandardItem();
    QStandardItem* nzbSizeItem = new QStandardItem();

    // add the nzb items to the model :
    int nzbNameItemRow = downloadModel->rowCount();

    downloadModel->setItem(nzbNameItemRow, FILE_NAME_COLUMN, nzbNameItem);
    downloadModel->setItem(nzbNameItemRow, SIZE_COLUMN, nzbSizeItem);
    downloadModel->setItem(nzbNameItemRow, STATE_COLUMN, nzbStateItem);
    downloadModel->setItem(nzbNameItemRow, PROGRESS_COLUMN, new QStandardItem());


    quint64 nzbFilesSize = 0;
    int par2FileNumber = 0;
    bool badCrc = false;

    // add children to parent (nzbNameItem) :
    foreach (GlobalFileData currentGlobalFileData, globalFileDataList) {

        // populate children :
        this->addParentItem(nzbNameItem, currentGlobalFileData);

        // compute size of all files contained in the nzb :
        nzbFilesSize += currentGlobalFileData.getNzbFileData().getSize();

        // count number of par2 Files :
        if (currentGlobalFileData.getNzbFileData().isPar2File()) {
            par2FileNumber++;
        }

        // if it is data restoring, check overall crc value in order to enable or disable smart par2 feature :
        if (currentGlobalFileData.getItemStatusData().getCrc32Match() != CrcOk) {
            badCrc = true;
        }

    }


    // set parent unique identifier:
    nzbNameItem->setData(QVariant(QUuid::createUuid().toString()), IdentifierRole);

    // set idle status by default :
    this->downloadModel->storeStatusDataToItem(nzbStateItem, ItemStatusData());

    // set size :
    nzbSizeItem->setData(qVariantFromValue(nzbFilesSize), SizeRole);


    // expand treeView :
    treeView->setExpanded(nzbNameItem->index(), Settings::expandTreeView());

    // alternate row color :
    treeView->setAlternatingRowColors(Settings::alternateColors());


    // enable / disable smart par2 download :
    if ( !badCrc && Settings::smartPar2Download() && (par2FileNumber < globalFileDataList.size()) ) {
        emit changePar2FilesStatusSignal(nzbNameItem->index(), WaitForPar2IdleStatus);
    }

}



void CentralWidget::addParentItem (QStandardItem* nzbNameItem, const GlobalFileData& currentGlobalFileData) {

    // get the current row of the nzbName item :
    int nzbNameItemNextRow = nzbNameItem->rowCount();

    const NzbFileData currentNzbFileData  = currentGlobalFileData.getNzbFileData();

    // add the file name as parent's item :
    QString fileName = currentNzbFileData.getFileName();
    QStandardItem* fileNameItem = new QStandardItem(fileName);
    nzbNameItem->setChild(nzbNameItemNextRow, FILE_NAME_COLUMN, fileNameItem);

    QStandardItem* parentStateItem = new QStandardItem();
    nzbNameItem->setChild(nzbNameItemNextRow, STATE_COLUMN, parentStateItem);

    QStandardItem* parentSizeItem = new QStandardItem();
    nzbNameItem->setChild(nzbNameItemNextRow, SIZE_COLUMN, parentSizeItem);

    QStandardItem* parentProgressItem = new QStandardItem();
    nzbNameItem->setChild(nzbNameItemNextRow, PROGRESS_COLUMN, parentProgressItem);


    // set data to fileNameItem :
    QVariant variant;
    variant.setValue(currentNzbFileData);
    fileNameItem->setData(variant, NzbFileDataRole);

    // set unique identifier :
    fileNameItem->setData(currentNzbFileData.getUniqueIdentifier(), IdentifierRole);
    // set tool tip :
    fileNameItem->setToolTip(fileName);

    // set idle status by default :
    nzbNameItem->setChild(nzbNameItemNextRow, STATE_COLUMN, parentStateItem);
    this->downloadModel->storeStatusDataToItem(parentStateItem, currentGlobalFileData.getItemStatusData());

    // set size :
    nzbNameItem->setChild(nzbNameItemNextRow, SIZE_COLUMN, parentSizeItem);
    parentSizeItem->setData(qVariantFromValue(currentNzbFileData.getSize()), SizeRole);

    // set download progression (0 by default) :
    nzbNameItem->setChild(nzbNameItemNextRow, PROGRESS_COLUMN, parentProgressItem);
    parentProgressItem->setData(qVariantFromValue(currentGlobalFileData.getProgressValue()), ProgressRole);

}



void CentralWidget::statusBarFileSizeUpdate() {

    quint64 size = 0;
    quint64 files = 0;

    // get the root item :
    QStandardItem* rootItem = downloadModel->invisibleRootItem();

    // parse nzb items :
    for (int i = 0; i < rootItem->rowCount(); i++) {

        QStandardItem* nzbItem = rootItem->child(i);

        // parse nzb children :
        for (int j = 0; j < nzbItem->rowCount(); j++) {

            QStandardItem* statusItem = nzbItem->child(j, STATE_COLUMN);

            // if the item is pending (Idle, Download, Paused, Pausing states), processes it :
            UtilityNamespace::ItemStatus status = downloadModel->getStatusFromStateItem(statusItem);

            if ( Utility::isReadyToDownload(status) ||
                 Utility::isPaused(status)          ||
                 Utility::isPausing(status) )       {

                QStandardItem* sizeItem = nzbItem->child(j, SIZE_COLUMN);
                size += sizeItem->data(SizeRole).toULongLong();
                files++;
            }
        }

    }


    this->clientsObserver->fullFileSizeUpdate(size, files);

}



void CentralWidget::setStartPauseDownload(const UtilityNamespace::ItemStatus targetStatus, const QList<QModelIndex>& indexesList){


    foreach (QModelIndex currentModelIndex, indexesList){

        // get file name item related to selected index :
        QStandardItem* fileNameItem = downloadModel->getFileNameItemFromIndex(currentModelIndex);

        // if the item is a nzbItem, retrieve their children :
        if (downloadModel->isNzbItem(fileNameItem)){

            for (int i = 0; i < fileNameItem->rowCount(); i++){

                QStandardItem* nzbChildrenItem = fileNameItem->child(i, FILE_NAME_COLUMN);
                segmentManager->setIdlePauseSegments(nzbChildrenItem, targetStatus);
            }
        }

        else {
            // update selected nzb children segments :
            segmentManager->setIdlePauseSegments(fileNameItem, targetStatus);
        }

    }

    // reset default buttons :
    treeView->selectedItemSlot();
}



void CentralWidget::setStartPauseDownloadAllItems(const UtilityNamespace::ItemStatus targetStatus){

    // select all rows in order to set them to paused or Idle :
    QList<QModelIndex> indexesList;
    int rowNumber = downloadModel->rowCount();

    for (int i = 0; i < rowNumber; i++) {

        QModelIndex currentIndex = downloadModel->item(i)->index();
        QStandardItem* stateItem = downloadModel->getStateItemFromIndex(currentIndex);

        UtilityNamespace::ItemStatus currentStatus = downloadModel->getStatusFromStateItem(stateItem);

        if ( ( (targetStatus == PauseStatus) && Utility::isReadyToDownload(currentStatus) ) ||
             ( (targetStatus == IdleStatus)  && Utility::isPausedOrPausing(currentStatus) ) ) {
            indexesList.append(currentIndex);
        }
    }

    this->setStartPauseDownload(targetStatus, indexesList);


}


void CentralWidget::retryDownload(const QModelIndexList& indexList) {

    foreach (QModelIndex currentModelIndex, indexList) {

        bool changeItemStatus = false;

        // by default consider that item does not need to be downloaded again :
        ItemStatus itemStatusResetTarget = ExtractFinishedStatus;

        // get file name item related to selected index :
        QStandardItem* fileNameItem = downloadModel->getFileNameItemFromIndex(currentModelIndex);

        // if current item is a nzbItem, retrieve their children :
        if (downloadModel->isNzbItem(fileNameItem)) {

            // check that at least on child will have its status reset to IdleStatus for requesting a new download.
            // It can happens (especially if user manually removes several files) that
            // all children are reverted bask to DecodeFinishStatus with no child reset in queue.
            // Check that this case does not happen :
            bool childStatusConsistencyCorrect = false;
            for (int i = 0; i < fileNameItem->rowCount(); i++) {

                QStandardItem* nzbChildrenItem = fileNameItem->child(i, FILE_NAME_COLUMN);
                if (this->modelQuery->isRetryDownloadAllowed(nzbChildrenItem) == IdleStatus) {

                    childStatusConsistencyCorrect = true;
                    break;

                }

            }
            // if at leat one child item is reset to IdleStatus, then allow download retry :
            if (childStatusConsistencyCorrect) {

                for (int i = 0; i < fileNameItem->rowCount(); i++) {

                    QStandardItem* nzbChildrenItem = fileNameItem->child(i, FILE_NAME_COLUMN);
                    itemStatusResetTarget = this->modelQuery->isRetryDownloadAllowed(nzbChildrenItem);

                    // if itemStatusResetTarget is different from ExtractFinishedStatus, retry download is allowed :
                    if (itemStatusResetTarget != ExtractFinishedStatus) {

                        this->itemParentUpdater->getItemChildrenManager()->resetItemStatusToTarget(nzbChildrenItem, itemStatusResetTarget);
                        changeItemStatus = true;

                    }

                }

            }
        }
        // else current item is a child :
        else {
            // update selected nzb children segments :
            itemStatusResetTarget = this->modelQuery->isRetryDownloadAllowed(fileNameItem);

            // reset current child item :
            if (itemStatusResetTarget != ExtractFinishedStatus) {

                this->itemParentUpdater->getItemChildrenManager()->resetItemStatusToTarget(fileNameItem, itemStatusResetTarget);

                fileNameItem = fileNameItem->parent();
                this->itemParentUpdater->getItemChildrenManager()->resetFinishedChildrenItemToDecodeFinish(fileNameItem);

                changeItemStatus = true;
            }

        }

        // finish to update *parent* status :
        if (changeItemStatus) {

            ItemStatusData itemStatusData = this->downloadModel->getStatusDataFromIndex(fileNameItem->index());
            itemStatusData.downloadRetry(IdleStatus);

            this->downloadModel->updateStatusDataFromIndex(fileNameItem->index(), itemStatusData);

        }

    }

    // update status bar and notify nntp clients :
    this->downloadWaitingPar2Slot();

}



void CentralWidget::initFoldersSettings() {

    // set default path for download and temporary folders if not filled by user :
    if (Settings::completedFolder().path().isEmpty()) {
        Settings::setCompletedFolder(QDir::homePath() + "/kwooty/Download");
    }

    if (Settings::temporaryFolder().path().isEmpty()) {
        Settings::setTemporaryFolder(QDir::homePath() + "/kwooty/Temp");
    }

}



SegmentManager* CentralWidget::getSegmentManager() const{
    return this->segmentManager;
}

StandardItemModel* CentralWidget::getDownloadModel() const{
    return this->downloadModel;
}

StandardItemModelQuery* CentralWidget::getModelQuery() const{
    return this->modelQuery;
}

MyTreeView* CentralWidget::getTreeView() const{
    return this->treeView;
}

ItemParentUpdater* CentralWidget::getItemParentUpdater() const{
    return this->itemParentUpdater;
}

ShutdownManager* CentralWidget::getShutdownManager() const{
    return this->shutdownManager;
}

ClientsObserver* CentralWidget::getClientsObserver() const{
    return this->clientsObserver;
}


FileOperations* CentralWidget::getFileOperations() const{
    return this->fileOperations;
}

QueueFileObserver* CentralWidget::getQueueFileObserver() const{
    return this->queueFileObserver;
}

DataRestorer* CentralWidget::getDataRestorer() const{
    return this->dataRestorer;
}

ServerManager* CentralWidget::getServerManager() const{
    return this->serverManager;
}

SegmentsDecoderThread* CentralWidget::getSegmentsDecoderThread() const {
    return this->segmentsDecoderThread;
}

SideBar* CentralWidget::getSideBar() const{
    return ((MainWindow*)this->parent())->getSideBar();
}


//============================================================================================================//
//                                               SLOTS                                                        //
//============================================================================================================//

void CentralWidget::statusBarFileSizeUpdateSlot(StatusBarUpdateType statusBarUpdateType){

    if (statusBarUpdateType == Reset) {
        // reset the status bar :
        this->clientsObserver->fullFileSizeUpdate(0, 0);
    }

    if (statusBarUpdateType == Incremental) {
        this->statusBarFileSizeUpdate();
    }
}


void CentralWidget::pauseDownloadSlot(){

    QList<QModelIndex> indexesList = treeView->selectionModel()->selectedRows();
    this->setStartPauseDownload(PauseStatus, indexesList);
}


void CentralWidget::startDownloadSlot(){

    QList<QModelIndex> indexesList = treeView->selectionModel()->selectedRows();
    this->setStartPauseDownload(IdleStatus, indexesList);
    emit dataHasArrivedSignal();
}

void CentralWidget::startAllDownloadSlot() {

    // ensure that any previous save file error message box is closed before starting pending downloads again :
    if (this->saveErrorButtonCode == 0) {
        this->setStartPauseDownloadAllItems(UtilityNamespace::IdleStatus);
        emit dataHasArrivedSignal();
    }
}

void CentralWidget::pauseAllDownloadSlot() {
    this->setStartPauseDownloadAllItems(UtilityNamespace::PauseStatus);
}


void CentralWidget::retryDownloadSlot() {   

    this->retryDownload(treeView->selectionModel()->selectedRows());

}


void CentralWidget::downloadWaitingPar2Slot(){

    this->statusBarFileSizeUpdate();
    emit dataHasArrivedSignal();

}



void CentralWidget::saveFileErrorSlot(const int fromProcessing){

    // 1. set all Idle items to pause before notify the user :
    this->setStartPauseDownloadAllItems(UtilityNamespace::PauseStatus);


    // 2. notify user with a message box (and avoid multiple message box instances):
    if (this->saveErrorButtonCode == 0) {

        QString saveErrorFolder;

        if (fromProcessing == DuringDecode) {
            saveErrorFolder = i18n("download folder");
        }
        if (fromProcessing == DuringDownload) {
            saveErrorFolder = i18n("temporary folder");
        }


        this->saveErrorButtonCode = KMessageBox::Cancel;
        this->saveErrorButtonCode = KMessageBox::messageBox(this,
                                                            KMessageBox::Sorry,
                                                            i18n("Write error in <b>%1</b>: disk drive may be full.<br>Downloads have been suspended.",
                                                                 saveErrorFolder),
                                                            i18n("Write error"));


        if (this->saveErrorButtonCode == KMessageBox::Ok) {
            this->saveErrorButtonCode = 0;
        }

    }

}


void CentralWidget::extractPasswordRequiredSlot(QString currentArchiveFileName) {

    KPasswordDialog kPasswordDialog(this);

    kPasswordDialog.setPrompt(i18n("The archive <b>%1</b> is password protected. <br>Please enter the password to extract the file.",  currentArchiveFileName));

    // if password has been entered :
    if(kPasswordDialog.exec()) {
        emit passwordEnteredByUserSignal(true, kPasswordDialog.password());
    }
    else {
        // else password has not been entered :
        emit passwordEnteredByUserSignal(false);
    }


}


void CentralWidget::updateSettingsSlot() {

    // delegate specific settings to concerned object  :
    emit settingsChangedSignal();

}
