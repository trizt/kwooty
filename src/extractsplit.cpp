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

#include "extractsplit.h"

#include <KUrl>
#include "kwooty_debug.h"

#include <QFile>
#include <QFileInfo>

#include "extractbase.h"
#include "repairdecompressthread.h"
#include "fileoperations.h"
#include "jobs/concatsplitfilesjob.h"

ExtractSplit::ExtractSplit(RepairDecompressThread *parent): ExtractBase(parent)
{

    this->mArchiveFormat = SplitFileFormat;

    this->mConcatSplitFilesJob = new ConcatSplitFilesJob(this);

    // get joinning progression from joining thread :
    connect(this->mConcatSplitFilesJob, SIGNAL(progressPercentSignal(int,QString)), this, SLOT(jobPercentSlot(int,QString)));

    // be notified when joining file thread has finished :
    connect(this->mConcatSplitFilesJob, SIGNAL(resultSignal(int)), this, SLOT(jobFinishSlot(int)));

}

ExtractSplit::~ExtractSplit()
{
    delete this->mConcatSplitFilesJob;
}

void ExtractSplit::launchProcess(const NzbCollectionData &nzbCollectionData, ExtractBase::ArchivePasswordStatus, bool, const QString &)
{

    this->mNzbCollectionData = nzbCollectionData;

    // get archive saved path and joined target file name :
    QString joinFileName;
    QString fileSavePath;
    this->retrieveFullPathJoinFileName(nzbCollectionData, fileSavePath, joinFileName);
    this->mNzbFileDataList = this->retrieveSplitFilesOnly(fileSavePath);

    // start the job :
    emit joinFilesSignal(this->mNzbFileDataList, fileSavePath, joinFileName);

}

QList<NzbFileData> ExtractSplit::retrieveSplitFilesOnly(const QString &fileSavePath) const
{

    QList<NzbFileData> nzbFileDataFilteredList;

    foreach (const NzbFileData &currentNzbFileData, this->mNzbCollectionData.getNzbFileDataList()) {

        // get current file :
        QFile currentSplitFile(Utility::buildFullPath(fileSavePath, currentNzbFileData.getDecodedFileName()));

        // check if it is a split file (check for .001, .002, etc... pattern) :
        if (FileOperations::isSplitFileFormat(currentSplitFile)) {
            nzbFileDataFilteredList.append(currentNzbFileData);
        }
    }

    return nzbFileDataFilteredList;
}

void ExtractSplit::retrieveFullPathJoinFileName(const NzbCollectionData &nzbCollectionData, QString &fileSavePath, QString &joinFileName) const
{

    NzbFileData currentNzbFileData = this->getFirstArchiveFileFromList(nzbCollectionData.getNzbFileDataList());

    // get archive saved path :
    fileSavePath = currentNzbFileData.getFileSavePath();
    joinFileName = QFileInfo(Utility::buildFullPath(fileSavePath, currentNzbFileData.getDecodedFileName())).completeBaseName();

}

void ExtractSplit::removeRenamedArchiveFile(const NzbFileData &)
{

    // /!\  reimplemented from ExtractBase : at this stage if renamedFileName is not empty,
    // it may probably correspond to the 'joined file' name if repair processing previously occurred.
    // it is important to not try to remove the 'joined file', in that case nothing have to be done.

}

void ExtractSplit::preRepairProcessing(const NzbCollectionData &nzbCollectionData)
{

    QString fileSavePath;
    QString joinFileName;

    this->retrieveFullPathJoinFileName(nzbCollectionData, fileSavePath, joinFileName);

    Utility::removeData(Utility::buildFullPath(fileSavePath, joinFileName));
}

//==============================================================================================//
//                                         SLOTS                                                //
//==============================================================================================//

void ExtractSplit::jobPercentSlot(int progress, const QString &fileNameStr)
{

    // search corresponding file into the list :
    this->findItemAndNotifyUser(fileNameStr, ExtractStatus, BothItemsTarget);

    // emit to files with ExtractStatus as status, the extract progression value :
    this->emitProgressToArchivesWithCurrentStatus(ExtractStatus, BothItemsTarget, progress);

}

void ExtractSplit::jobFinishSlot(int errorCode)
{

    // post processing when job is complete (if no error occurred during the job, job->error() == 0) :
    this->extractFinishedSlot(errorCode, QProcess::NormalExit);

}

// the following methods are not used for joining split files job :
QStringList ExtractSplit::createProcessArguments(const QString &, const QString &, const bool &, const QString &)
{
    return QStringList();
}
void ExtractSplit::extractUpdate(const QString &) {}
void ExtractSplit::checkIfArchivePassworded(const QString &, bool &) {}
void ExtractSplit::sendExtractProgramNotFoundNotification() {}
QString ExtractSplit::searchExtractProgram()
{
    return QString();
}
