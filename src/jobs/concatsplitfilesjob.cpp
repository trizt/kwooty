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

#include "concatsplitfilesjob.h"

#include <KUrl>
#include "kwooty_debug.h"

#include <QFile>
#include <QFileInfo>
#include <QTimer>

#include "fileoperations.h"
#include "extractsplit.h"

ConcatSplitFilesJob::ConcatSplitFilesJob(ExtractSplit *parent)
{

    mDedicatedThread = new QThread();
    moveToThread(mDedicatedThread);

    qRegisterMetaType< QList<NzbFileData> >("QList<NzbFileData>");
    // launch joining process from parent :
    connect(parent,
            &ExtractSplit::joinFilesSignal,
            this,
            &ConcatSplitFilesJob::joinFilesSlot);

    // start current thread :
    mDedicatedThread->start();

}

ConcatSplitFilesJob::~ConcatSplitFilesJob()
{

    mDedicatedThread->quit();
    // if query close is requested during joining file process,
    // forces kwooty to close by terminating the running thread (maximum 3 seconds later) :
    mDedicatedThread->wait(3000);

    delete mDedicatedThread;
}

void ConcatSplitFilesJob::joinFilesSlot(const QList<NzbFileData> &nzbFileDataList, const QString &fileSavePath, const QString &joinFileName)
{

    mNzbFileDataList = nzbFileDataList;
    mFileSavePath = fileSavePath;
    mJoinFileName = joinFileName;

    // join split files together :
    bool processFailed = joinSplittedFiles();

    int error = QProcess::NormalExit;
    if (processFailed) {
        // set a value different from zero (QProcess::NormalExit) to notify read/write error :
        error = 255;
    }

    emit resultSignal(error);
}

bool ConcatSplitFilesJob::joinSplittedFiles()
{

    bool processFailed = false;
    int fileProcessedNumber = 0;

    // retrieve and open joined file :
    QFile joinFile(Utility::buildFullPath(mFileSavePath, mJoinFileName));

    // check if file already exists :
    bool joinFileExists = joinFile.exists();

    joinFile.open(QIODevice::WriteOnly | QIODevice::Append);

    // join split files (list of files is assumed to be ordered at this stage) :
    foreach (const NzbFileData& currentNzbFileData, mNzbFileDataList) {

        fileProcessedNumber++;

        // get current archive name :
        QString archiveName = currentNzbFileData.getDecodedFileName();

        QString fullPathArchiveName = Utility::buildFullPath(mFileSavePath, archiveName);

        // if file already exists, it means that it has been created during repair processing.
        // in that case target file is already joined and this process does not have to be done :
        if (!joinFileExists) {

            // retrieve split file number :
            bool conversionOk;
            int splitFileNumber = QFileInfo(fullPathArchiveName).suffix().toInt(&conversionOk);

            // check that the file is a real split file and that the current splitted file
            // is just following the previous one (eg : if .002 is missing, avoid to join .001, .003) :
            if (!conversionOk ||
                    (fileProcessedNumber != splitFileNumber)) {

                processFailed = true;
                break;
            }

            QFile currentSplitFile(fullPathArchiveName);

            // open split file :
            if (!currentSplitFile.open(QIODevice::ReadOnly)) {
                processFailed = true;
                break;
            }

            // write content in joined file :
            if (joinFile.write(currentSplitFile.readAll()) == -1) {
                processFailed = true;
                break;
            }

            // close split file :
            currentSplitFile.close();
        }

        // emit progress percentage :
        int progress = qRound((double)(fileProcessedNumber * PROGRESS_COMPLETE / mNzbFileDataList.size()));
        emit progressPercentSignal(progress, archiveName);

    }

    // job done, close file :
    joinFile.close();

    // if process failed, remove the incomplete joined file :
    if (processFailed) {
        joinFile.remove();
    }

    return processFailed;

}

