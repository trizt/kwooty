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

#include "itemchildrenmanager.h"

#include "kwooty_debug.h"

#include <QPointer>

#include "core.h"
#include "standarditemmodel.h"
#include "itemparentupdater.h"
#include "segmentmanager.h"
#include "kwootysettings.h"

ItemChildrenManager::ItemChildrenManager(Core *parent, ItemParentUpdater *itemParentUpdater) : ItemAbstractUpdater(parent->getDownloadModel(), ItemAbstractUpdater::Child)
{

    this->mParent = parent;
    this->mItemParentUpdater = itemParentUpdater;

    // set smartPar2Download setting value :
    this->mSmartPar2Download = Settings::smartPar2Download();

    // setup connections :
    this->setupConnections();

}

void ItemChildrenManager::setupConnections()
{

    // parent notify that settings have been changed :
    connect(mParent,
            SIGNAL(settingsChangedSignal()),
            this,
            SLOT(settingsChangedSlot()));

    // download par2 files if crc check failed during archive download :
    connect(this,
            SIGNAL(downloadWaitingPar2Signal()),
            mItemParentUpdater,
            SIGNAL(downloadWaitingPar2Signal()));

}

bool ItemChildrenManager::resetItemStatusIfExtractFail(const QModelIndex &index)
{

    bool par2NotDownloaded = false;
    bool extractFail = false;

    // check if some par2 files have not been downloaded :
    QStandardItem *parentItem = this->mDownloadModel->itemFromIndex(index);

    for (int i = 0; i < parentItem->rowCount(); ++i) {

        // get current item status :
        UtilityNamespace::ItemStatus childItemStatus = this->mDownloadModel->getChildStatusFromNzbIndex(index, i);

        if (childItemStatus == WaitForPar2IdleStatus) {
            par2NotDownloaded = true;
        }

        if ((childItemStatus == ExtractFailedStatus) ||
                (childItemStatus == ExtractBadCrcStatus)) {

            extractFail = true;
        }

    }

    bool par2Required = false;

    if (extractFail && par2NotDownloaded) {
        par2Required = true;
    }

    // if par2 are not downloaded, change items status :
    if (par2Required) {

        // reset rar files to decodeFinish status :
        for (int i = 0; i < parentItem->rowCount(); ++i) {

            QModelIndex childIndex = index.child(i, FILE_NAME_COLUMN);

            // get current item status :
            QStandardItem *stateItem = this->mDownloadModel->getStateItemFromIndex(childIndex);
            UtilityNamespace::ItemStatus childItemStatus = this->mDownloadModel->getStatusFromStateItem(stateItem);

            // reset item whose extracting failed to DecodeStatus
            // in order to verify them when par2 download is complete :
            if ((childItemStatus == ExtractFailedStatus) ||
                    (childItemStatus == ExtractBadCrcStatus)) {

                this->mDownloadModel->updateStateItem(childIndex, DecodeFinishStatus);

            }
        }

        // set par2 files from WaitForPar2IdleStatus to IdleStatus :
        this->changePar2FilesStatusSlot(index, IdleStatus);

    }

    return par2Required;

}

//============================================================================================================//
//                                               SLOTS                                                        //
//============================================================================================================//

void ItemChildrenManager::changePar2FilesStatusSlot(const QModelIndex &index, UtilityNamespace::ItemStatus itemStatus)
{

    // get itemStatusData :
    ItemStatusData nzbItemStatusData = this->mDownloadModel->getStatusDataFromIndex(index);

    // if crc fail status has not already been set :
    if (nzbItemStatusData.getCrc32Match() != CrcKoNotified) {

        bool par2StatusChanged = false;

        // get all nzb children :
        int rowNumber = this->mDownloadModel->itemFromIndex(index)->rowCount();

        for (int i = 0; i < rowNumber; ++i) {

            QModelIndex childIndex = index.child(i, FILE_NAME_COLUMN);
            NzbFileData currentNzbFileData = this->mDownloadModel->getNzbFileDataFromIndex(childIndex);

            if (currentNzbFileData.isPar2File()) {

                // get itemStatusData :
                ItemStatusData childItemStatusData = this->mDownloadModel->getStatusDataFromIndex(childIndex);

                if (!childItemStatusData.isDownloadFinish() &&
                        !Utility::isPausedOrPausing(childItemStatusData.getStatus())) {

                    // set par2 item from IdleStatus to WaitForPar2IdleStatus and vice versa :
                    this->mDownloadModel->updateStateItem(childIndex, itemStatus);

                    par2StatusChanged = true;

                }

            }

        }

        if (par2StatusChanged) {

            // recalculate nzbSize in order get to get a proper progress computation
            // regarding if par2 have to be downloaded or not :
            mItemParentUpdater->recalculateNzbSize(index);

        }
    }

}

void ItemChildrenManager::resetItemStatusToTarget(QStandardItem *fileNameItem, const ItemStatus &itemStatusResetTarget)
{

    // if current item has to be downloaded again :
    if (itemStatusResetTarget == IdleStatus) {

        QPointer<SegmentManager> segmentManager = this->mParent->getSegmentManager();

        if (segmentManager) {
            segmentManager->setIdleDownloadFailSegments(fileNameItem);
        }

    }

    ItemStatusData itemStatusData = this->mDownloadModel->getStatusDataFromIndex(fileNameItem->index());
    itemStatusData.downloadRetry(itemStatusResetTarget);

    this->mDownloadModel->updateStatusDataFromIndex(fileNameItem->index(), itemStatusData);

}

void ItemChildrenManager::resetFinishedChildrenItemToDecodeFinish(QStandardItem *fileNameItem)
{

    for (int i = 0; i < fileNameItem->rowCount(); ++i) {

        QStandardItem *nzbChildrenItem = fileNameItem->child(i, FILE_NAME_COLUMN);

        ItemStatusData itemStatusData = this->mDownloadModel->getStatusDataFromIndex(nzbChildrenItem->index());
        UtilityNamespace::ItemStatus nzbChildrenStatus = itemStatusData.getStatus();

        // if current file has already been decoded but post processing failed :
        if (itemStatusData.isDecodeFinish() &&
                Utility::isPostDownloadFailed(nzbChildrenStatus)) {

            // reset item to decode finish :
            itemStatusData.downloadRetry(DecodeFinishStatus);
            this->mDownloadModel->updateStatusDataFromIndex(nzbChildrenItem->index(), itemStatusData);

        }
    }

}

void ItemChildrenManager::settingsChangedSlot()
{

    // settings have been changed, enable or disable smart par2 download :
    if (this->mSmartPar2Download != Settings::smartPar2Download()) {

        // get the root model :
        QStandardItem *rootItem = this->mDownloadModel->invisibleRootItem();

        // for each parent item, update it children :
        for (int i = 0; i < rootItem->rowCount(); ++i) {

            QStandardItem *parentItem = rootItem->child(i, FILE_NAME_COLUMN);

            // enable or disable smart par2 download :
            UtilityNamespace::ItemStatus itemStatus = IdleStatus;

            // if smart par2 download enabled, set all par2 files to WaitForPar2IdleStatus :
            if (Settings::smartPar2Download()) {
                itemStatus = WaitForPar2IdleStatus;
            }

            // change smart par2 status :
            this->changePar2FilesStatusSlot(parentItem->index(), itemStatus);

        }

        // recalculate file number and size to statusBar and eventually launch par2 download :
        emit downloadWaitingPar2Signal();

        // update smartPar2Download :
        this->mSmartPar2Download = Settings::smartPar2Download();

    }

}

