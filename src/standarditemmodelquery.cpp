/***************************************************************************
 *   Copyright (C) 2011 by Xavier Lefage                                   *
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

#include "standarditemmodelquery.h"

#include "standarditemmodel.h"
#include "core.h"
#include "mainwindow.h"
#include "observers/queuefileobserver.h"
#include "kwootysettings.h"

StandardItemModelQuery::StandardItemModelQuery(Core *core) : QObject(core)
{

    this->mCore = core;
    this->mDownloadModel = core->getDownloadModel();

}

QStandardItem *StandardItemModelQuery::searchParentItem(const SearchItemStatus &searchItemStatus)
{

    QStandardItem *stateItem = 0;

    // get the root model :
    QStandardItem *rootItem = this->mDownloadModel->invisibleRootItem();

    // get the first parent with download active :
    for (int i = 0; i < rootItem->rowCount(); ++i) {

        QStandardItem *parentStateItem = rootItem->child(i, STATE_COLUMN);
        UtilityNamespace::ItemStatus currentStatus = this->mDownloadModel->getStatusFromStateItem(parentStateItem);

        if (searchItemStatus == SearchItemIdle) {

            // check if parent status is idle :
            if (currentStatus == UtilityNamespace::IdleStatus) {

                stateItem = parentStateItem;
                break;
            }
        } else if (searchItemStatus == SearchItemDownloadOrPausing) {

            // check if parent status is either downloading or pausing :
            if (Utility::isDownloadOrPausing(currentStatus)) {

                stateItem = parentStateItem;
                break;
            }
        } else if (searchItemStatus == SearchItemPause) {

            // check if parent status is either in pause :
            if (Utility::isPaused(currentStatus)) {

                stateItem = parentStateItem;
                break;
            }
        } else if (searchItemStatus == SearchItemPostDownloadProcessing) {

            // check if parent status is currently being post processed :
            if (Utility::isPostDownloadProcessing(currentStatus)) {

                stateItem = parentStateItem;
                break;
            }
        }

    }

    return stateItem;

}

QStandardItem *StandardItemModelQuery::searchParentItemIdle()
{
    return this->searchParentItem(SearchItemIdle);
}

QStandardItem *StandardItemModelQuery::searchParentItemDownloadOrPausing()
{
    return this->searchParentItem(SearchItemDownloadOrPausing);
}

QStandardItem *StandardItemModelQuery::searchParentItemPause()
{
    return this->searchParentItem(SearchItemPause);
}

QStandardItem *StandardItemModelQuery::searchParentItemPostDownloadProcessing()
{
    return this->searchParentItem(SearchItemPostDownloadProcessing);
}

QList<QModelIndex> StandardItemModelQuery::retrieveDecodeFinishParentIndexList() const
{

    QList<QModelIndex> downloadFinishParentIndexList;

    for (int i = 0; i < this->mDownloadModel->rowCount(); ++i) {

        QModelIndex parentIndex = this->mDownloadModel->item(i)->index();

        // for earch parent item, check if download has been finished :
        if (this->mDownloadModel->getStatusDataFromIndex(parentIndex).isDecodeFinish()) {

            downloadFinishParentIndexList.append(parentIndex);

        }

    }

    return downloadFinishParentIndexList;

}

QStandardItem *StandardItemModelQuery::retrieveParentFileNameItemFromUuid(const QString &parentUuidStr)
{

    QStandardItem *parentFileNameItem = 0;

    for (int i = 0; i < this->mDownloadModel->rowCount(); ++i) {

        if (parentUuidStr == this->mDownloadModel->getUuidStrFromIndex(this->mDownloadModel->item(i)->index())) {

            parentFileNameItem = this->mDownloadModel->item(i);
            break;
        }

    }

    return parentFileNameItem;
}

QList<QModelIndex> StandardItemModelQuery::retrieveStartPauseIndexList(const UtilityNamespace::ItemStatus targetStatus) const
{

    // select all rows in order to set them to paused or Idle :
    QList<QModelIndex> indexesList;
    int rowNumber = this->mDownloadModel->rowCount();

    for (int i = 0; i < rowNumber; ++i) {

        QModelIndex currentIndex = this->mDownloadModel->item(i)->index();
        QStandardItem *stateItem = this->mDownloadModel->getStateItemFromIndex(currentIndex);

        UtilityNamespace::ItemStatus currentStatus = this->mDownloadModel->getStatusFromStateItem(stateItem);

        if (((targetStatus == PauseStatus) && Utility::isReadyToDownload(currentStatus)) ||
                ((targetStatus == IdleStatus)  && Utility::isPausedOrPausing(currentStatus))) {

            indexesList.append(currentIndex);

        }
    }

    return indexesList;
}

bool StandardItemModelQuery::isRootModelEmpty() const
{
    return (!this->mDownloadModel->invisibleRootItem()->hasChildren());
}

bool StandardItemModelQuery::areJobsFinished() const
{

    bool jobFinished = true;

    // at first check that no plugin are doing post processing :
    if (this->mCore->getQueueFileObserver()->isPluginJobRunning()) {

        jobFinished = false;
    }

    else {
        // get the root model :
        QStandardItem *rootItem = this->mDownloadModel->invisibleRootItem();

        // for each parent item, get its current status :
        for (int i = 0; i < rootItem->rowCount(); ++i) {

            QStandardItem *parentStateItem = rootItem->child(i, STATE_COLUMN);
            UtilityNamespace::ItemStatus currentStatus = this->mDownloadModel->getStatusFromStateItem(parentStateItem);

            // check parent status activity :
            if (Utility::isReadyToDownload(currentStatus)       ||
                    Utility::isPausing(currentStatus)               ||
                    Utility::isDecoding(currentStatus)              ||
                    Utility::isPostDownloadProcessing(currentStatus)) {

                jobFinished = false;
                break;
            }

            // if do not shutdown system if paused items found :
            if (Settings::pausedShutdown() && Utility::isPaused(currentStatus)) {

                jobFinished = false;
                break;
            }

        }

    }

    return jobFinished;
}

bool StandardItemModelQuery::haveItemsSameParent(const QList<QModelIndex> &indexesList)
{

    bool sameParent = true;

    if (!indexesList.isEmpty()) {

        // get the parent of the first selected element :
        QModelIndex firstParentIndex = indexesList.at(0).parent();

        for (int i = 1; i < indexesList.size(); ++i) {

            QModelIndex currentModelIndex = indexesList.at(i);

            // if elements do not have the same parent :
            if (firstParentIndex != currentModelIndex.parent()) {
                sameParent = false;
                break;
            }
        }
    } else {
        sameParent = false;
    }

    return sameParent;

}

bool StandardItemModelQuery::isParentContainsPar2File(QStandardItem *item) const
{

    bool containsPar2File = false;

    // if current item is a child, retrieve its parent :
    QStandardItem *parentFileNameItem = this->mDownloadModel->getNzbItem(item);

    for (int i = 0; i < parentFileNameItem->rowCount(); ++i) {

        QStandardItem *nzbChildrenItem = parentFileNameItem->child(i, FILE_NAME_COLUMN);
        NzbFileData nzbFileData = this->mDownloadModel->getNzbFileDataFromIndex(nzbChildrenItem->index());

        if (nzbFileData.isPar2File()) {

            containsPar2File = true;
            break;
        }
    }

    return containsPar2File;

}

ItemStatus StandardItemModelQuery::isRetryDownloadAllowed(QStandardItem *fileNameItem, bool *allowRetry)
{

    bool changeItemStatus = false;

    // by default consider that item does not need to be downloaded again :
    ItemStatus itemStatusResetTarget = ExtractFinishedStatus;

    ItemStatusData itemStatusData = this->mDownloadModel->getStatusDataFromIndex(fileNameItem->index());
    //qCDebug(KWOOTY_LOG) <<  "status:" << itemStatusData.getDataStatus() << "crc:" << itemStatusData.getCrc32Match() << "decodeFinish:" << itemStatusData.isDecodeFinish() << "downloadFinish:" <<itemStatusData.isDownloadFinish();

    ItemStatusData parentItemStatusData = itemStatusData;
    // if current item is a child, retrieve its parent :
    if (!mDownloadModel->isNzbItem(fileNameItem)) {
        parentItemStatusData = this->mDownloadModel->getStatusDataFromIndex(fileNameItem->parent()->index());
    }

    // only allow to retry download if post download processing is not running and
    // post processing failed for at least some items :
    if (!Utility::isPostDownloadProcessing(parentItemStatusData.getStatus()) &&
            !parentItemStatusData.areAllPostProcessingCorrect()) {

        // item has been postprocessed :
        if (itemStatusData.getStatus() >= VerifyStatus) {

            // if file has been correctly verified, do not enable "Retry button",
            // but item have to be set to DecodeFinish status if retry action comes from parent item :
            if (Utility::isVerifyFileCorrect(itemStatusData.getStatus())) {
                itemStatusResetTarget = DecodeFinishStatus;
            } else if (Utility::isPostDownloadFailed(itemStatusData.getStatus())) {
                changeItemStatus = true;
            }
        }
        // else item has been decoded :
        else if (itemStatusData.isDecodeFinish()) {

            // item is decoded and crc does not match, try to download file again :
            if (itemStatusData.getCrc32Match() != UtilityNamespace::CrcOk) {
                changeItemStatus = true;
            }
            // else crc matches and item status indicates that post processing has already been performed,
            // item does not need to be downloaded again :
            else if (itemStatusData.getStatus() != DecodeFinishStatus) {
                itemStatusResetTarget = DecodeFinishStatus;
                changeItemStatus = true;
            }
        }
        // else item may have been downloaded but not decoded due to missing segments :
        else if (itemStatusData.isDownloadFinish()) {

            if (itemStatusData.getDataStatus() == NoData ||
                    itemStatusData.getDataStatus() == DataIncomplete) {
                changeItemStatus = true;
            }
        }

        // if item have to be changed :
        if (changeItemStatus &&
                itemStatusResetTarget == ExtractFinishedStatus) {

            itemStatusResetTarget = IdleStatus;
        }

    }

    if (allowRetry) {
        *allowRetry = changeItemStatus;
    }

    return itemStatusResetTarget;

}

bool StandardItemModelQuery::isManualRepairExtractAllowed(QStandardItem *fileNameItem) const
{

    bool manualRepairExtractAllowed = false;

    // check that selected item is a parent :
    if (this->mDownloadModel->isNzbItem(fileNameItem)) {

        ItemStatusData nzbItemStatusData = this->mDownloadModel->getStatusDataFromIndex(fileNameItem->index());

        // if parent status is "DecodeFinish" and automatic post process is disabled,
        // manual extract is allowed :
        if (nzbItemStatusData.isPostProcessFinish() &&
                nzbItemStatusData.getStatus() != ExtractFinishedStatus) {

            manualRepairExtractAllowed = true;
        }

    }

    return manualRepairExtractAllowed;

}

bool StandardItemModelQuery::isParentFileNameExists(const QString &nzbName) const
{

    bool parentFileNameExists = false;

    // get the root model :
    QStandardItem *rootItem = this->mDownloadModel->invisibleRootItem();

    // for each parent item, get its current nzb file name :
    for (int i = 0; i < rootItem->rowCount(); ++i) {

        QStandardItem *parentFileNameItem = rootItem->child(i, FILE_NAME_COLUMN);

        if (parentFileNameItem->text() == nzbName) {

            parentFileNameExists = true;
            break;
        }

    }

    return parentFileNameExists;

}
