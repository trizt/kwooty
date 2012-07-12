/***************************************************************************
 *   Copyright (C) 2012 by Xavier Lefage                                   *
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


#include "categories.h"

#include <KDebug>
#include <klocale.h>
#include "kdiskfreespaceinfo.h"

#include <QDir>
#include <QDirIterator>

#include "mainwindow.h"
#include "core.h"
#include "categoriesfilehandler.h"
#include "categoriesplugin.h"
#include "categoriesmodel.h"
#include "standarditemmodel.h"
#include "standarditemmodelquery.h"
#include "data/itemstatusdata.h"
#include "data/nzbfiledata.h"
#include "utilitycategories.h"

#include "kwootysettings.h"
#include "kwooty_categoriessettings.h"



Categories::Categories(CategoriesPlugin* parent) :  QObject(parent) {

    this->core = parent->getMainWindow()->getCore();

    // get model :
    this->categoriesModel = CategoriesFileHandler().loadModelFromFile(this);

    this->jobProcessing = false;

    // init categories behavior :
    this->settingsChanged();

    // setup signals/slots connections :
    this->setupConnections();

    this->moveStatusTextMap.insert(NoMoveStatus,                           i18n("Transfer not performed"));
    this->moveStatusTextMap.insert(MovingStatus,                           i18n("Transferring..."));
    this->moveStatusTextMap.insert(MoveSuccessStatus,                      i18n("Transfer complete"));
    this->moveStatusTextMap.insert(MoveUserCanceledErrorStatus,            i18n("Transfer canceled"));
    this->moveStatusTextMap.insert(MoveDiskFullErrorStatus,                i18n("Transfer error (disk is full"));
    this->moveStatusTextMap.insert(MoveCouldNotMkdirErrorStatus,           i18n("Transfer error (target folder can not be created)"));
    this->moveStatusTextMap.insert(MoveInsufficientDiskSpaceErrorStatus,   i18n("Transfer error (insufficient disk space)"));
    this->moveStatusTextMap.insert(MoveUnknownErrorStatus,                 i18n("Transfer error"));


    // associate color to display according to item status :
    this->moveStatusColorMap.insert(NoMoveStatus,                           QPalette().color(QPalette::WindowText));
    this->moveStatusColorMap.insert(MovingStatus,                           QPalette().color(QPalette::WindowText));
    this->moveStatusColorMap.insert(MoveSuccessStatus,                      QPalette().color(QPalette::WindowText));
    this->moveStatusColorMap.insert(MoveUserCanceledErrorStatus,            QColor("orangered"));
    this->moveStatusColorMap.insert(MoveDiskFullErrorStatus,                QColor("orangered"));
    this->moveStatusColorMap.insert(MoveCouldNotMkdirErrorStatus,           QColor("orangered"));
    this->moveStatusColorMap.insert(MoveInsufficientDiskSpaceErrorStatus,   QColor("orangered"));
    this->moveStatusColorMap.insert(MoveUnknownErrorStatus,                 QColor("orangered"));

}




Categories::~Categories() {

}


void Categories::setupConnections() {

    connect(this->core->getDownloadModel(),
            SIGNAL(parentStatusItemChangedSignal(QStandardItem*, ItemStatusData)),
            this,
            SLOT(parentStatusItemChangedSlot(QStandardItem*)));

}


void Categories::launchPreProcess() {

    if ( !this->jobProcessing &&
         this->uuidItemList.size() > 0 ) {

        MimeData mimeDataChildFound(MimeData::SubCategory);

        StandardItemModel* downloadModel = this->core->getDownloadModel();

        this->currentUuidItem = this->uuidItemList.takeFirst();
        QStandardItem* parentFileNameItem = this->core->getModelQuery()->retrieveParentFileNameItemFromUuid(this->currentUuidItem);


        // retrieve downloaded folder path :
        QString nzbFileSavepath = downloadModel->getParentFileSavePathFromIndex(parentFileNameItem->index());

        // retrieve all mime types and the corresponding sizes of files :
        QHash<QString, quint64> mimeNameSizeMap = this->scanDownloadedFiles(nzbFileSavepath);

        // try to guess the main mime type of the overall folder :
        QString mimeName = this->guessMainMimeName(mimeNameSizeMap);
        kDebug() << "mime type : " << mimeName;

        // if mime type has been found :
        if (!mimeName.isEmpty()) {

            QString mainCategory = UtilityCategories::buildMaincategoryPattern(mimeName);
            QString subCategory = UtilityCategories::buildSubcategoryPattern(mimeName);

            // search corresponding main category in model :
            QStandardItem* categoryItem = this->categoriesModel->retrieveItemFromCategory(mainCategory);

            // if main category has been defined :
            if (categoryItem) {

                kDebug() << "main category :" << mainCategory;

                // retrrieve all subcategory data :
                QList<MimeData> mimeDataChildList = this->categoriesModel->retrieveMimeDataListFromItem(categoryItem);

                // compare subcategories stored with guessed subcategory :
                foreach (MimeData mimeDataChild, mimeDataChildList) {

                    // if subCategory has been found :
                    if (subCategory == mimeDataChild.getSubCategory()) {
                        mimeDataChildFound = mimeDataChild;
                        break;
                    }

                }

            }

        }

        // prepare folder moving :
        this->moveJobStatus = NoMoveStatus;

        // be sure that target folder path has been filled by user in settings :
        if (!mimeDataChildFound.getMoveFolderPath().isEmpty()) {

            // check disk space from downloaded folder to target folder :
            if (this->checkDiskSpace(mimeDataChildFound, nzbFileSavepath, mimeNameSizeMap.values())) {

                this->moveJobStatus = MovingStatus;
                this->notifyMoveProcessing(UtilityNamespace::PROGRESS_INIT);

                // update file save path in nzb parent item in order to get "Downloads" action from tool bar get into it :
                downloadModel->updateParentFileSavePathFromIndex(parentFileNameItem->index(), mimeDataChildFound.getMoveFolderPath());

                // mime type has been identified, downloaded files can be moved to target folder :
                this->launchMoveProcess(mimeDataChildFound, nzbFileSavepath);

            }
            else {
                this->moveJobStatus = MoveInsufficientDiskSpaceErrorStatus;
            }

            this->notifyMoveProcessing();
        }


    } // if post processing correct

}



void Categories::launchMoveProcess(const MimeData& mimeData, const QString& nzbFileSavepath) {

    this->jobProcessing = true;

    // default values :
    KIO::JobFlag jobFlag = KIO::DefaultFlags;

    QString folderName = QDir(nzbFileSavepath).dirName();
    QString moveFolderPath = mimeData.getMoveFolderPath() + '/' + folderName;

    // try to create target folder :
    Utility::createFolder(mimeData.getMoveFolderPath());

    // check if folder in move folder path has to be overwritten :
    if (CategoriesSettings::transferManagement() > 0) {
        jobFlag = KIO::Overwrite;
    }
    // else create a new folder name :
    else {

        // get name of the folder to transfer :


        // check if this folder already exists in move folder path :
        if (QDir(moveFolderPath).exists()) {

            for (int i = 1; i < 100; i++) {

                // rename folder with extension, eg : folder.1 :
                QString candidateAbsoluteFileName = moveFolderPath + "." + QString::number(i);

                if (!QDir(candidateAbsoluteFileName).exists()) {

                    moveFolderPath = candidateAbsoluteFileName;
                    break;
                }
            }
        }
    }

    // create job :
    KIO::CopyJob* moveJob = KIO::move(KUrl(nzbFileSavepath), KUrl(moveFolderPath), jobFlag);

    // setup connections with job :
    connect(moveJob, SIGNAL(result(KJob*)), this, SLOT(handleResultSlot(KJob*)));
    connect(moveJob, SIGNAL(moving(KIO::Job*, const KUrl& , const KUrl&)), this, SLOT(jobProgressionSlot(KIO::Job*)));

    moveJob->start();

}


QString Categories::guessMainMimeName(const QHash<QString, quint64>& mimeNameSizeMap) {

    QString guessedMainMimeName;

    // get size of files with the same mime type :
    QList<quint64> fileSizeList = mimeNameSizeMap.values();

    // order file sizes :
    qSort(fileSizeList);

    if (fileSizeList.size() > 0) {
        // return the mimeName that correspond the biggest added files size :
        guessedMainMimeName = mimeNameSizeMap.key(fileSizeList.takeLast());

    }

    return guessedMainMimeName;

}


bool Categories::checkDiskSpace(const MimeData& mimeData, const QString& nzbFileSavepath, const QList<quint64>& sizeList) {

    // compute the total size of files to be moved :
    quint64 totalSizeToMove = 0;

    foreach (quint64 currentSize, sizeList) {
        totalSizeToMove += currentSize;
    }

    // get available free space from target folder :
    quint64 availableFreeDiskSpace = KDiskFreeSpaceInfo::freeSpaceInfo(mimeData.getMoveFolderPath()).available();

    bool sufficientDiskSpace = false;

    // if move folder has to be performed on the same mount point :
    if ( KDiskFreeSpaceInfo::freeSpaceInfo(nzbFileSavepath).mountPoint() ==
         KDiskFreeSpaceInfo::freeSpaceInfo(mimeData.getMoveFolderPath()).mountPoint() ) {

        kDebug() << "same mount point :" << KDiskFreeSpaceInfo::freeSpaceInfo(nzbFileSavepath).mountPoint();

        // be sure that there is at least minimum free disk space available during move process :
        if (availableFreeDiskSpace > totalSizeToMove / 100) {
            sufficientDiskSpace = true;
        }

    }
    else {

        kDebug() << "different mount point :" << KDiskFreeSpaceInfo::freeSpaceInfo(nzbFileSavepath).mountPoint() << KDiskFreeSpaceInfo::freeSpaceInfo(mimeData.getMoveFolderPath()).mountPoint();

        // if the mount point is different, check that at least all total size to move is available on target :
        if ( availableFreeDiskSpace > (totalSizeToMove + totalSizeToMove / 100) ) {
            sufficientDiskSpace = true;

        }
        else {
            kDebug() << "not enough free space" << availableFreeDiskSpace << totalSizeToMove;
        }

    }

    return sufficientDiskSpace;

}



KSharedPtr<KMimeType> Categories::retrieveFileMimeType(const QString& currentFileStr, const QString& nzbFileSavepath) {

    QString absoluteFilePath = nzbFileSavepath + '/' + currentFileStr;

    // try to get mime type by file name :
    KSharedPtr<KMimeType> mimeType = KMimeType::findByUrl(KUrl(absoluteFilePath), 0, true, false);

    // if mime type has not been identified :
    if (mimeType->isDefault()) {

        kDebug() << "mime type not identified !!" << mimeType->name() << mimeType->isDefault();
        kDebug() << "try to get mime type from content file :" << absoluteFilePath;

        QFile currentFile(absoluteFilePath);
        currentFile.open(QIODevice::ReadOnly);

        mimeType = KMimeType::findByContent(&currentFile);

        currentFile.close();

    }

    return mimeType;
}



QHash<QString, quint64> Categories::scanDownloadedFiles(const QString& nzbFileSavepath) {

    // collect total file size for earch different mime type :
    QHash<QString, quint64> mimeNameSizeMap;

    // get all subdirectories from downloaded folder :
    QDirIterator dirIterator(nzbFileSavepath,
                             QDir::AllDirs | QDir::NoDotDot,
                             QDirIterator::Subdirectories);

    // parse all directories :
    while (dirIterator.hasNext()) {

        QString currentDirectory = dirIterator.next();

        kDebug() << "### QDirIterator :" << currentDirectory;

        // retrieves all files from current directory :
        QStringList currentFileList = QDir(currentDirectory).entryList(QDir::Files | QDir::NoDotAndDotDot, QDir::Size);

        // get mime type of each file :
        QFileInfo fileInfo;
        foreach (QString currentFileStr, currentFileList) {

            KSharedPtr<KMimeType> mimeType = this->retrieveFileMimeType(currentFileStr, nzbFileSavepath);

            // mime type has been identified :
            if (!mimeType->isDefault()) {

                fileInfo.setFile(currentDirectory + '/' + currentFileStr);

                // add size of files with the same mime type :
                mimeNameSizeMap.insert(mimeType->name(),
                                       mimeNameSizeMap.value(mimeType->name()) + static_cast<quint64>(qAbs(fileInfo.size())));

                if (!fileInfo.exists()) {
                    kDebug() << "ooops, file does not exists :" << fileInfo.absoluteFilePath();
                }

                kDebug() << "NAME" << mimeType->name() << "SIZE" <<  mimeNameSizeMap.value(mimeType->name()) ;

            }

        }
    }

    return mimeNameSizeMap;
}



void Categories::notifyMoveProcessing(int progress) {

    QStandardItem* stateItem = 0;

    // retrieve nzb file name item from its uuid :
    QStandardItem* parentFileNameItem = this->core->getModelQuery()->retrieveParentFileNameItemFromUuid(this->currentUuidItem);

    // if file name item has been found, then retrieve its corresponding state item :
    if (parentFileNameItem) {
        stateItem = this->core->getDownloadModel()->getStateItemFromIndex(parentFileNameItem->index());
    }

    kDebug() << "progress" << this->moveStatusTextMap.value(this->moveJobStatus);

    // update state item :
    if (stateItem) {

        stateItem->setText(this->moveStatusTextMap.value(this->moveJobStatus));
        stateItem->setForeground(this->moveStatusColorMap.value(this->moveJobStatus));

        if (this->moveJobStatus >= MoveUserCanceledErrorStatus) {
            progress = UtilityNamespace::PROGRESS_COMPLETE;
        }

        // update progress item :
        if (progress <= UtilityNamespace::PROGRESS_COMPLETE) {

            // store progress value in model in order to notify itemdelegate that will paint
            // the progress bar accordingly :
            this->core->getDownloadModel()->updateProgressItem(stateItem->index(), progress);

        }

    }

}


//============================================================================================================//
//                                               SLOTS                                                        //
//============================================================================================================//

void Categories::parentStatusItemChangedSlot(QStandardItem* stateItem) {

    StandardItemModel* downloadModel = this->core->getDownloadModel();
    ItemStatusData itemStatusData = downloadModel->getStatusDataFromIndex(stateItem->index());

    // if post-processing of nzb file is correct, try to move downloaded folder into target folder :
    if ( itemStatusData.isPostProcessFinish() &&
         itemStatusData.areAllPostProcessingCorrect() ) {

        kDebug() << "post processing correct";

        // store uuid's item for asynchronous job progress notify :
        QString uuidItem = downloadModel->getUuidStrFromIndex(stateItem->index());

        if (!this->uuidItemList.contains(uuidItem)) {

            this->uuidItemList.append(uuidItem);

        }

        this->launchPreProcess();

    }

}


void Categories::handleResultSlot(KJob* moveJob) {

    int error = moveJob->error();

    switch (error) {

    case KIO::ERR_DISK_FULL: {
        this->moveJobStatus = MoveDiskFullErrorStatus;
        break;
    }

    case KIO::ERR_USER_CANCELED: {
        this->moveJobStatus = MoveUserCanceledErrorStatus;
        break;
    }

    case KIO::ERR_CANNOT_RENAME: {
        this->moveJobStatus = MoveCouldNotMkdirErrorStatus;
        break;
    }
    default: {
        this->moveJobStatus = MoveUnknownErrorStatus;
        break;
    }
    }

    if (error > 0) {
        kDebug() << "move job error :" << moveJob->errorText();
    }
    else {
        this->moveJobStatus = MoveSuccessStatus;
    }

    this->notifyMoveProcessing(UtilityNamespace::PROGRESS_COMPLETE);

    // job has ended, no more notifying have to be done :
    this->jobProcessing = false;

    // check if there is another item waiting :
    this->launchPreProcess();


}


void Categories::jobProgressionSlot(KIO::Job* moveJob) {

    kDebug() << "progress :" << moveJob->percent();

    this->notifyMoveProcessing(static_cast<int>(moveJob->percent()));
}


void Categories::settingsChanged() {

    // reload settings from just saved config file :
    CategoriesSettings::self()->readConfig();

    // reload model :
    this->categoriesModel = CategoriesFileHandler().loadModelFromFile(this);

}