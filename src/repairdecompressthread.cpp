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

#include "repairdecompressthread.h"

#include "kwooty_debug.h"
#include <QFileInfo>
#include "core.h"
#include "repair.h"
#include "extractrar.h"
#include "extractzip.h"
#include "extractsplit.h"
#include "itemparentupdater.h"
#include "segmentmanager.h"

RepairDecompressThread::RepairDecompressThread() {}

RepairDecompressThread::RepairDecompressThread(Core *parent)
{

    this->mParent = parent;

    this->init();

    this->mDedicatedThread = new QThread();
    this->moveToThread(this->mDedicatedThread);

    // start current thread :
    this->mDedicatedThread->start();

}

RepairDecompressThread::~RepairDecompressThread()
{

    this->mDedicatedThread->quit();
    this->mDedicatedThread->wait();

    delete this->mDedicatedThread;

}

Core *RepairDecompressThread::getCore()
{

    return this->mParent;

}

void RepairDecompressThread::init()
{

    this->mWaitForNextProcess = false;

    // create verify/repair instance :
    mRepair = new Repair(this);

    // create extract instances :
    this->mExtracterList.append(new ExtractRar(this));
    this->mExtracterList.append(new ExtractZip(this));
    this->mExtracterList.append(new ExtractSplit(this));

    this->mRepairDecompressTimer = new QTimer(this);

    // setup connections :
    this->setupConnections();

}

void RepairDecompressThread::setupConnections()
{

    // check if there are data to repair :
    connect(this->mRepairDecompressTimer ,
            SIGNAL(timeout()),
            this,
            SLOT(processJobSlot()));

    // receive data to repair and decompress from ItemParentUpdater :
    qRegisterMetaType<NzbCollectionData>("NzbCollectionData");
    connect(this->mParent->getItemParentUpdater(),
            SIGNAL(repairDecompressSignal(NzbCollectionData)),
            this,
            SLOT(repairDecompressSlot(NzbCollectionData)));

    // send repairing related updates to segmentmanager :
    qRegisterMetaType<QVariant>("QVariant");
    qRegisterMetaType<UtilityNamespace::ItemTarget>("UtilityNamespace::ItemTarget");

    // begin file extraction when end repair signal has been received :
    connect(mRepair,
            SIGNAL(repairProcessEndedSignal(NzbCollectionData)),
            this,
            SLOT(repairProcessEndedSlot(NzbCollectionData)));

    // update info about repair and extract process :
    qRegisterMetaType<PostDownloadInfoData>("PostDownloadInfoData");
    connect(this,
            SIGNAL(updateRepairExtractSegmentSignal(PostDownloadInfoData)),
            this->mParent->getSegmentManager(),
            SLOT(updateRepairExtractSegmentSlot(PostDownloadInfoData)));

}

ExtractBase *RepairDecompressThread::retrieveCorrespondingExtracter(const NzbCollectionData &currentNzbCollectionData)
{

    ExtractBase *properExtracter = 0;

    // get archive format (rar, zip, 7z or split) :
    UtilityNamespace::ArchiveFormat archiveFormat = this->getArchiveFormatFromList(currentNzbCollectionData.getNzbFileDataList());

    // return the associated extracter according to archive format :
    foreach (ExtractBase *extracter, this->mExtracterList) {

        if (extracter->canHandleFormat(archiveFormat)) {

            properExtracter = extracter;
            break;

        }

    }

    return properExtracter;
}

void RepairDecompressThread::preRepairProcessing(const NzbCollectionData &currentNzbCollectionData)
{

    // check archive format :
    ExtractBase *extracter = this->retrieveCorrespondingExtracter(currentNzbCollectionData);

    // if format is split files only, check that the joined file does not exists already (may happen when a .nzb is dowloaded twice)
    // this is required by extraSplit job in order to know if joined file has been created during repair process or not :
    if (extracter) {
        extracter->preRepairProcessing(currentNzbCollectionData);
    }

}

bool RepairDecompressThread::isListContainsdifferentGroups(const QList<NzbFileData> &nzbFileDataList)
{

    bool containsDifferentGroups = true;
    QSet<QString> baseNamePar2Set;
    QSet<QString> baseNameRarSet;

    foreach (const NzbFileData &nzbFileData, nzbFileDataList) {

        if (nzbFileData.isPar2File()) {
            QString parBaseName = this->getBaseNameFromPar2(nzbFileData);
            baseNamePar2Set.insert(parBaseName);

        }
        if (nzbFileData.isArchiveFile()) {
            QString rarBaseName = this->getBaseNameFromRar(nzbFileData);
            baseNameRarSet.insert(rarBaseName);
        }

    }

    // The sets will contain all different base names founds for the nzb group
    // if sets are equal to one means that files belonging to nzb group are not mixed with other files.
    // There sets are separated because parSet could have been renamed and thought does not have the same base
    // name as rarSet :
    if ((baseNamePar2Set.size() == 1) &&
            (baseNameRarSet.size() == 1)) {

        containsDifferentGroups = false;

    }

    return containsDifferentGroups;
}

QStringList RepairDecompressThread::listDifferentFileBaseName(NzbCollectionData &nzbCollectionData)
{

    QList<NzbFileData> nzbFileDataList = nzbCollectionData.takeNzbFileDataList();

    // at this step nzbFileData list contains archives without par2 files,
    // group archive files together :
    QStringList fileBaseNameList;

    for (int i = 0; i < nzbFileDataList.size(); ++i) {

        NzbFileData nzbFileData = nzbFileDataList.at(i);

        QString fileBaseName;
        if (nzbFileData.isArchiveFile()) {
            fileBaseName = this->getBaseNameFromRar(nzbFileData);
        }

        else if (nzbFileData.isPar2File()) {
            fileBaseName = this->getBaseNameFromPar2(nzbFileData);
        }

        // update base name to nzbFileData :
        nzbFileData.setBaseName(fileBaseName);
        nzbFileDataList.replace(i, nzbFileData);

        // add rarBaseName to the list :
        if ((!fileBaseName.isEmpty()) &&
                (!fileBaseNameList.contains(fileBaseName))) {

            fileBaseNameList.append(fileBaseName);

        }

    }

    // file list has been updated, set it into collection :
    nzbCollectionData.setNzbFileDataList(nzbFileDataList);

    // sort fileBaseNameList because par2 files contained in this list may not contain the file extension
    // as opposed to archive files. This is useful to correctly group matching par2 files and archive files together :
    qSort(fileBaseNameList);

    return fileBaseNameList;
}

QString RepairDecompressThread::getBaseNameFromPar2(const NzbFileData &nzbFileData)
{

    QString fileBaseName;

    QString par2FileWithoutExtension = nzbFileData.getDecodedFileName();
    // remove .par2 extension :
    par2FileWithoutExtension.chop(par2FileExt.size());

    // suppress .volxxx-+xxx extension :
    if (par2FileWithoutExtension.contains("vol")) {

        QRegExp regExp("(.*)(\\.vol\\d+.\\d+)");
        regExp.setCaseSensitivity(Qt::CaseInsensitive);
        if (regExp.exactMatch(par2FileWithoutExtension)) {
            fileBaseName = regExp.cap(1);
        }
    }
    // par2 file does not contains .volxxx-+xxx extension :
    else {
        fileBaseName = par2FileWithoutExtension;
    }

    return fileBaseName;
}

QString RepairDecompressThread::getBaseNameFromRar(const NzbFileData &nzbFileData)
{

    QString decodedFileName = nzbFileData.getDecodedFileName();

    // Archive name could be {baseName}.r** or {baseName}.part***.rar
    // determine baseName to group rar files (without associated par2) together :
    QFileInfo fileInfo(decodedFileName);

    // remove first extension .r** or .rar:
    QString fileBaseName = fileInfo.completeBaseName();

    // check if part*** is contained in baseName :
    fileInfo.setFile(fileBaseName);
    QString fileSuffix = fileInfo.suffix();

    if (fileSuffix.contains(QRegExp("part\\d+", Qt::CaseInsensitive))) {
        // remove .part*** extension :
        fileBaseName = fileInfo.completeBaseName();

    }

    return fileBaseName;
}

NzbFileData RepairDecompressThread::tryToGuessDecodedFileName(NzbFileData &targetNzbFileData, const QList<NzbFileData> &nzbFileDataList, const QString &fileBaseName)
{

    QString builtFileName;

    foreach (const NzbFileData &currentNzbFileData, nzbFileDataList) {

        // search a decoded file name containing the rarbase name pattern :
        if (currentNzbFileData.isArchiveFile() && currentNzbFileData.getDecodedFileName().contains(fileBaseName)) {

            int fileNamePos = targetNzbFileData.getFileName().indexOf(fileBaseName);
            if (fileNamePos > -1) {

                builtFileName = targetNzbFileData.getFileName().mid(fileNamePos, currentNzbFileData.getDecodedFileName().size());

                if (!builtFileName.isEmpty()) {

                    targetNzbFileData.setDecodedFileName(builtFileName);
                    targetNzbFileData.setBaseName(fileBaseName);

                    break;
                }

            }

        }

    }

    //qCDebug(KWOOTY_LOG) << "tryToGuessDecodedFileName from : " << targetNzbFileData.getFileName() << "BUILT NAME : " << builtFileName;
    return targetNzbFileData;

}

void RepairDecompressThread::processRarFilesFromDifferentGroups(const QStringList &fileBaseNameList, NzbCollectionData &nzbCollectionData)
{

    // group archives that own par2 files for verifying/repairing :
    if (!fileBaseNameList.isEmpty()) {

        QList<NzbFileData> nzbFileDataList = nzbCollectionData.takeNzbFileDataList();

        foreach (const QString &fileBaseName, fileBaseNameList) {

            QList<NzbFileData> groupedFileList;
            QString par2BaseName;

            foreach (NzbFileData nzbFileData, nzbFileDataList) {

                // the file is missing if the decoded file name is empty :
                if (nzbFileData.getDecodedFileName().isEmpty()  && !nzbFileData.isPar2File()) {
                    // in that case, try to build the decoded file name :
                    nzbFileData = this->tryToGuessDecodedFileName(nzbFileData, nzbFileDataList, fileBaseName);

                }

                // trying to get the most generic discriminant according to rar file base names and
                // par2 file base names that could not exactly match :
                if ((!nzbFileData.isPar2File() && nzbFileData.getDecodedFileName().contains(fileBaseName)) ||
                        (nzbFileData.isPar2File() && (nzbFileData.getBaseName() == fileBaseName))) {

                    // group nzbFileData with the same baseName :
                    groupedFileList.append(nzbFileData);

                    // remove the current nzbFileData to be sure that it will not be match
                    // another fileBaseName from fileBaseNameList :
                    nzbFileDataList.removeOne(nzbFileData);

                    // if par2Files holding base name have been found, set par2BaseName :
                    if (nzbFileData.isPar2File()) {
                        par2BaseName = fileBaseName + "*";
                    }
                }

            }

            // if par2BaseName is not empty, verify/repair will be proceeded,
            // if par2BaseName is empty, extraction will be done directly :
            if (!groupedFileList.isEmpty()) {

                nzbCollectionData.setPar2BaseName(par2BaseName);

                // sort file list by alphabetical order
                // in order to get archives files sorted from the first one to the last one (eg split.001, split.002, etc...):
                qSort(groupedFileList);
                nzbCollectionData.setNzbFileDataList(groupedFileList);

                this->mFilesToRepairList.append(nzbCollectionData);
            }
        }

    }

}

void RepairDecompressThread::processRarFilesFromSameGroup(NzbCollectionData &nzbCollectionData)
{

    QList<NzbFileData> nzbFileDataList = nzbCollectionData.takeNzbFileDataList();

    // get file base name for rar file :
    QString rarBaseName;
    foreach (const NzbFileData &nzbFileData, nzbFileDataList) {

        if (nzbFileData.isArchiveFile()) {
            rarBaseName = this->getBaseNameFromRar(nzbFileData);
            break;
        }
    }

    // set decodedFileName for missing files :
    QList<NzbFileData> groupedFileList;
    foreach (NzbFileData nzbFileData, nzbFileDataList) {

        // the file is missing if the decoded file name is empty :
        if (nzbFileData.getDecodedFileName().isEmpty()  && !nzbFileData.isPar2File()) {

            // in that case, try to build the decoded file name :
            nzbFileData = this->tryToGuessDecodedFileName(nzbFileData, nzbFileDataList, rarBaseName);

        }

        // decoded file name has been built or already exists, add it to list:
        if (!nzbFileData.getDecodedFileName().isEmpty()) {
            groupedFileList.append(nzbFileData);
        }
    }

    // in this case, par2Base name is not initialized in order to get only "*" as par2 argument during verifiying process :
    if (!groupedFileList.isEmpty()) {

        QString par2BaseName = "*";
        nzbCollectionData.setPar2BaseName(par2BaseName);

        // sort file list by alphabetical order
        // in order to get archives files sorted from the first one to the last one (eg split.001, split.002, etc...):
        qSort(groupedFileList);
        nzbCollectionData.setNzbFileDataList(groupedFileList);

        this->mFilesToRepairList.append(nzbCollectionData);
    }

}

UtilityNamespace::ArchiveFormat RepairDecompressThread::getArchiveFormatFromList(const QList<NzbFileData> &nzbFileDataListToExtract)
{

    UtilityNamespace::ArchiveFormat archiveFormat = UnknownArchiveFormat;

    foreach (const NzbFileData &nzbFileData, nzbFileDataListToExtract) {

        // files are grouped with unique archive type, get corresponding archive format :
        if (nzbFileData.isArchiveFile() &&
                (nzbFileData.getArchiveFormat() != UtilityNamespace::UnknownArchiveFormat)) {

            archiveFormat = nzbFileData.getArchiveFormat();
            break;
        }

    }

    return archiveFormat;

}

void RepairDecompressThread::emitProcessUpdate(const PostDownloadInfoData &repairDecompressInfoData)
{

    emit updateRepairExtractSegmentSignal(repairDecompressInfoData);
}

void RepairDecompressThread::notifyNzbProcessEnded(const NzbCollectionData &nzbCollectionData)
{

    // check that nzbCollectionData processing is finished :
    // /!\ nzbCollectionData.getNzbParentId() will be empty in case of extract processing cancelled due
    // to passworded files. Do not go further and let the user decides what to do with this nzb content now :
    if (!nzbCollectionData.getNzbParentId().isEmpty()) {

        if (!this->mFilesToRepairList.contains(nzbCollectionData) &&
                !this->mFilesToExtractList.contains(nzbCollectionData)) {

            PostDownloadInfoData repairDecompressInfoData;
            repairDecompressInfoData.initRepairDecompress(nzbCollectionData.getFirstChildUniqueIdentifier(), PROGRESS_COMPLETE, ExtractSuccessStatus, ParentItemTarget);

            // post process is fully over, also indicate if post process is correct or not :
            repairDecompressInfoData.setAllPostProcessingCorrect(nzbCollectionData.isAllPostProcessingCorrect());
            repairDecompressInfoData.setPostProcessFinish(true);

            this->emitProcessUpdate(repairDecompressInfoData);

            //qCDebug(KWOOTY_LOG) << "Post processing correct ? :" << nzbCollectionData.isAllPostProcessingCorrect();

        }

        // if post process has failed, notify of failure in other nzbCollectionData from the same nzb set :
        this->propagatePostProcessFailureToPendingCollection(this->mFilesToRepairList, nzbCollectionData);
        this->propagatePostProcessFailureToPendingCollection(this->mFilesToExtractList, nzbCollectionData);

    }

}

void RepairDecompressThread::propagatePostProcessFailureToPendingCollection(QList<NzbCollectionData> &filesToProcessList, const NzbCollectionData &nzbCollectionData)
{

    if (filesToProcessList.contains(nzbCollectionData)) {

        int index = filesToProcessList.indexOf(nzbCollectionData);
        NzbCollectionData pendingNzbCollectionData = filesToProcessList.value(index);

        pendingNzbCollectionData.setAllPostProcessingCorrect(nzbCollectionData.isAllPostProcessingCorrect());
        filesToProcessList.replace(index, pendingNzbCollectionData);

    }

}

//==============================================================================================//
//                                         SLOTS                                                //
//==============================================================================================//

void RepairDecompressThread::processJobSlot()
{

    // pre-process job : group archive volumes together :
    this->processPendingFiles();

    // repair and verify files :
    this->startRepairSlot();

    // extract files :
    this->startExtractSlot();

    // stop timer if there is no more pending jobs :
    if (!this->mWaitForNextProcess &&
            this->mFilesToRepairList.isEmpty() &&
            this->mFilesToExtractList.isEmpty() &&
            this->mFilesToProcessList.isEmpty()) {

        this->mRepairDecompressTimer->stop();
    }

}

void RepairDecompressThread::startRepairSlot()
{

    // if no data is currently being verified :
    if (!this->mWaitForNextProcess && !this->mFilesToRepairList.isEmpty()) {

        this->mWaitForNextProcess = true;

        // get nzbCollectionData to be verified from list :
        NzbCollectionData currentNzbCollectionData = this->mFilesToRepairList.takeFirst();

        // perform some pre-processing dedicated to "split-only" files :
        this->preRepairProcessing(currentNzbCollectionData);

        // verify data only if par has been found :
        if (!currentNzbCollectionData.getPar2BaseName().isEmpty() &&
                currentNzbCollectionData.isRepairProcessAllowed()) {

            mRepair->launchProcess(currentNzbCollectionData);
        } else {

            currentNzbCollectionData.setVerifyRepairTerminateStatus(RepairFinishedStatus);
            this->repairProcessEndedSlot(currentNzbCollectionData);
        }

    }

}

void RepairDecompressThread::startExtractSlot()
{

    // if no data is currently being verified :
    if (!this->mFilesToExtractList.isEmpty()) {

        NzbCollectionData nzbCollectionDataToExtract = this->mFilesToExtractList.takeFirst();

        // extract data :
        if (!nzbCollectionDataToExtract.getNzbFileDataList().isEmpty() &&
                nzbCollectionDataToExtract.isExtractProcessAllowed()) {

            ExtractBase *extracter = this->retrieveCorrespondingExtracter(nzbCollectionDataToExtract);

            // extract with unrar, 7z or built-in file joiner job :
            if (extracter) {
                extracter->launchProcess(nzbCollectionDataToExtract);
            }
            // if archive format is unknown, abort extracting process :
            else {
                this->extractProcessEndedSlot(nzbCollectionDataToExtract);
            }

        } else {
            this->extractProcessEndedSlot(nzbCollectionDataToExtract);
        }

    }

}

void RepairDecompressThread::repairProcessEndedSlot(const NzbCollectionData &nzbCollectionDataToExtract)
{

    UtilityNamespace::ItemStatus repairStatus = nzbCollectionDataToExtract.getVerifyRepairTerminateStatus();

    // if verify/repair ok, launch archive extraction :
    if (repairStatus == RepairFinishedStatus) {
        this->mFilesToExtractList.append(nzbCollectionDataToExtract);
    }

    // if verify/repair ko, do not launch archive extraction and verify next pending items :
    if (repairStatus == RepairFailedStatus) {
        this->mWaitForNextProcess = false;
    }

    // if repair has failed, notify that nzb processing has ended now :
    this->notifyNzbProcessEnded(nzbCollectionDataToExtract);

}

void RepairDecompressThread::extractProcessEndedSlot(const NzbCollectionData &nzbCollectionData)
{

    bool triggerPar2Download = false;

    // **case of multi set nzb** => remove other nzb sets as the first extraction failed,
    // par2 files will have to be downloaded without extracting *now* the others parts of the nzb.
    // all nzb-set will then be extracted when par2 files will be downloaded :
    if ((nzbCollectionData.getExtractTerminateStatus() == ExtractFailedStatus) &&
            (nzbCollectionData.getPar2FileDownloadStatus() == WaitForPar2IdleStatus)) {

        // remove multi nzb-parts with the same parent ID :
        if (mFilesToRepairList.contains(nzbCollectionData)) {
            mFilesToRepairList.removeAll(nzbCollectionData);
        }

        triggerPar2Download = true;
    }

    // if extract process has ended, notify that nzb processing is now finished :
    if (!triggerPar2Download) {
        this->notifyNzbProcessEnded(nzbCollectionData);
    }

    this->mWaitForNextProcess = false;

}

void RepairDecompressThread::repairDecompressSlot(const NzbCollectionData &nzbCollectionData)
{

    // all files have been downloaded and decoded, set them in the pending list
    // in order to process them :
    this->mFilesToProcessList.append(nzbCollectionData);

    // start timer in order to process pending jobs :
    if (!this->mRepairDecompressTimer->isActive()) {
        this->mRepairDecompressTimer->start(100);
    }

}

void RepairDecompressThread::processPendingFiles()
{

    if (!this->mFilesToProcessList.isEmpty()) {

        // get nzbFileDataList to verify and repair :
        NzbCollectionData nzbCollectionData = this->mFilesToProcessList.takeFirst();

        // if the list contains rar files belonging to one group :
        if (!this->isListContainsdifferentGroups(nzbCollectionData.getNzbFileDataList())) {

            this->processRarFilesFromSameGroup(nzbCollectionData);

        } else {
            // get each baseName list :
            QStringList fileBaseNameList = this->listDifferentFileBaseName(nzbCollectionData);

            //qCDebug(KWOOTY_LOG) << "fileBaseNameList" << fileBaseNameList;

            // group files together according to their names in order to verify them :
            this->processRarFilesFromDifferentGroups(fileBaseNameList, nzbCollectionData);
        }

        // processing could already be ended, notify main thread :
        this->notifyNzbProcessEnded(nzbCollectionData);
    }

}

