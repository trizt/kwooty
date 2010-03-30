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

#include "extractbase.h"

#include <KDebug>
#include <QFile>
#include "settings.h"
#include "repairdecompressthread.h"
#include "data/nzbfiledata.h"


ExtractBase::ExtractBase(RepairDecompressThread* parent) {

    this->parent = parent;

    this->extractProcess = new KProcess(this);
    this->setupConnections();
    this->resetVariables();

}


ExtractBase::~ExtractBase() {
    this->extractProcess->close();
}


void ExtractBase::setupConnections() {

    qRegisterMetaType<QProcess::ExitStatus>("QProcess::ExitStatus");

    connect (this->extractProcess, SIGNAL(readyRead()), this, SLOT(extractReadyReadSlot()));
    connect (this->extractProcess, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(extractFinishedSlot(int, QProcess::ExitStatus)));


    // send extract related updates to segmentmanager :
    connect (this ,
             SIGNAL(updateExtractSignal(QVariant, int, UtilityNamespace::ItemStatus, UtilityNamespace::ItemTarget)),
             this->parent,
             SIGNAL(updateExtractSignal(QVariant, int, UtilityNamespace::ItemStatus, UtilityNamespace::ItemTarget)));

    // begin verify - repair process for new pending files when signal has been received :
    connect (this,
             SIGNAL(extractProcessEndedSignal()),
             this->parent,
             SLOT(extractProcessEndedSlot()));

    // display dialog box when password is required for archive extraction :
    connect (this,
             SIGNAL(extractPasswordRequiredSignal(QString)),
             parent,
             SLOT(extractPasswordRequiredSlot(QString)));

    // send password entered by the user :
    connect (this->parent,
             SIGNAL(passwordEnteredByUserSignal(bool, QString)),
             this,
             SLOT(passwordEnteredByUserSlot(bool, QString)));


}



void ExtractBase::launchProcess(const QList<NzbFileData>& nzbFileDataList, ExtractBase::ArchivePasswordStatus archivePasswordStatus, bool passwordEnteredByUSer, QString passwordStr){

    this->nzbFileDataList = nzbFileDataList;
    this->archivePasswordStatus = archivePasswordStatus;

    // search unrar program at each process launch in case settings have been changed at anytime :
    this->extractProgramPath = this->searchExtractProgram();

    // launch extract if unrar program found :
    if (this->isExtractProgramFound) {

        // look for archive name (renamed name or decoded name) to set as argument :
        QString archiveName;

        NzbFileData currentNzbFileData = this->getFirstArchiveFileFromList();

        // get archive saved path :
        QString fileSavePath = currentNzbFileData.getFileSavePath();

        // case archives have been renamed, get original file name :
        if (!currentNzbFileData.getRenamedFileName().isEmpty()) {
            archiveName = currentNzbFileData.getRenamedFileName();
        }
        // otherwise get the decoded file name (default behavior) :
        else {
            archiveName = currentNzbFileData.getDecodedFileName();
        }

        QStringList args = this->createProcessArguments(archiveName, fileSavePath, passwordEnteredByUSer, passwordStr);

        //kDebug() << "ARGS :" << this->extractProgramPath <<args;

        this->extractProcess->setTextModeEnabled(true);
        this->extractProcess->setOutputChannelMode(KProcess::MergedChannels);
        this->extractProcess->setNextOpenMode(QIODevice::ReadWrite | QIODevice::Unbuffered);
        this->extractProcess->setProgram(this->extractProgramPath, args);
        this->extractProcess->start();


        // if path contains "*" add "\\" in order to avoid issues with QRegExp pattern search :
        //this->fileSavePath.replace("*", "\\*");

    }
    // unrar or 7z program has not been found, notify user :
    else {
        this->sendExtractProgramNotFoundNotification();
    }

}



//==============================================================================================//
//                                       processing                                             //
//==============================================================================================//



NzbFileData ExtractBase::getFirstArchiveFileFromList() const {

    NzbFileData currentNzbFileData;
    foreach (NzbFileData nzbFileData, this->nzbFileDataList) {

        if (nzbFileData.isArchiveFile()) {
            //return the first achive file from list :
            currentNzbFileData = nzbFileData;
            break;
        }
    }

    return currentNzbFileData;
}



void ExtractBase::updateNzbFileDataInList(NzbFileData& currentNzbFileData, const UtilityNamespace::ItemStatus extractStep, const int index) {

    currentNzbFileData.setExtractProgressionStep(extractStep);
    this->nzbFileDataList.replace(index, currentNzbFileData);

}


void ExtractBase::resetVariables(){

    this->isExtractProgramFound = false;
    this->fileNameToExtract.clear();
    this->nzbFileDataList.clear();
    this->stdOutputLines.clear();
    this->extractProcess->close();
    this->extractProgressValue = PROGRESS_INIT;
}


void ExtractBase::removeArchiveFiles(){

    foreach (NzbFileData nzbFileData, this->nzbFileDataList) {

        if (nzbFileData.getExtractProgressionStep() == ExtractStatus) {

            // removed decoded file name :
            QString fullPathFileName = nzbFileData.getFileSavePath() + nzbFileData.getDecodedFileName();
            Utility::removeData(fullPathFileName);

            // par2 may have rewrote repaired file names with ".1" ext, try to remove them :
            Utility::removeData(fullPathFileName + ".1");

            // if file has been renamed, par2 may have created the archive with the original name suppress it :
            if (!nzbFileData.getRenamedFileName().isEmpty()) {

                fullPathFileName = nzbFileData.getFileSavePath() + nzbFileData.getRenamedFileName();
                Utility::removeData(fullPathFileName);

            }
        }

    }
}




//==============================================================================================//
//                                         SLOTS                                                //
//==============================================================================================//

void ExtractBase::extractReadyReadSlot(){

    bool passwordCheckIsNextLine = false;

    // process each lines received :
    this->stdOutputLines += QString::fromUtf8(extractProcess->readAllStandardOutput());

    QStringList lines = this->stdOutputLines.split("\n");
    foreach (QString line, lines) {

        if (!line.isEmpty()) {

            //kDebug() << "line : " << line;

            if (this->archivePasswordStatus == ExtractBase::ArchiveCheckIfPassworded) {
                this->checkIfArchivePassworded(line, passwordCheckIsNextLine);
            }

            this->extractUpdate(line);
        }

    }

    // remove complete lines :
    if (this->stdOutputLines.endsWith("\n")) {
        this->stdOutputLines.clear();
    }
    // keep last line which is not complete :
    else {
        this->stdOutputLines = lines.takeLast();
    }

}



void ExtractBase::extractFinishedSlot(const int exitCode, const QProcess::ExitStatus exitStatus) {

    //kDebug() << "exitCode" << exitCode << " exitStatus " << exitStatus;

    // password checking has ended, files are *not* passworded, launch extract now :
    if (archivePasswordStatus == ExtractBase::ArchiveIsNotPassworded) {

        this->extractProcess->close();
        this->launchProcess(this->nzbFileDataList, ExtractBase::ArchivePasswordCheckEnded);

    }
    // password checking has ended, files are passworded, display password box to user :
    else if (this->archivePasswordStatus == ExtractBase::ArchiveIsPassworded) {

        NzbFileData nzbFileData = this->getFirstArchiveFileFromList();
        emit extractPasswordRequiredSignal(nzbFileData.getDecodedFileName());

    }

    // file extraction has ended :
    else {

        // notify repairDecompressThread that extraction is over :
        emit extractProcessEndedSignal();

        // 1. exit without errors :
        if (exitStatus == QProcess::NormalExit && exitCode == QProcess::NormalExit ){

            this->emitFinishToArchivesWithoutErrors(ExtractSuccessStatus, PROGRESS_COMPLETE);

            // remove rar files if extract succeed :
            if (Settings::removeArchiveFiles()) {
                this->removeArchiveFiles();
            }

        }
        // 2. exit with errors :
        else {
            this->emitFinishToArchivesWithoutErrors(ExtractFailedStatus, PROGRESS_COMPLETE);
        }


        // notify parent that extraction has finished :
        NzbFileData nzbFileData = this->getFirstArchiveFileFromList();
        emit updateExtractSignal(nzbFileData.getUniqueIdentifier(), PROGRESS_COMPLETE, ExtractFinishedStatus, ParentItemTarget);

        this->resetVariables();

    }

}



void ExtractBase::passwordEnteredByUserSlot(bool passwordEntered, QString password) {

    // this slot is shared between 7zextract and rarextract instances
    // do processing for proper instance : the one whih archivePasswordStatus set to ArchiveIsPassworded :
    if (this->archivePasswordStatus == ExtractBase::ArchiveIsPassworded) {

        // set password entered by user to the extract process :
        if (passwordEntered) {
            this->launchProcess(this->nzbFileDataList, ExtractBase::ArchivePasswordCheckEnded, passwordEntered, password);
        }
        // password has not been entered, stop extract process :
        else {

            // notify children that extraction failed :
            this->emitStatusToAllArchives(PROGRESS_COMPLETE, ExtractFailedStatus, ChildItemTarget);

            // notify parent that extraction has finished :
            NzbFileData nzbFileData = this->getFirstArchiveFileFromList();
            emit updateExtractSignal(nzbFileData.getUniqueIdentifier(), PROGRESS_COMPLETE, ExtractFinishedStatus, ParentItemTarget);

            this->resetVariables();

            // notify repairDecompressThread that extraction is over :
            emit extractProcessEndedSignal();
        }

    }

}



//==============================================================================================//
//                                     emit  SIGNALS                                            //
//==============================================================================================//


void ExtractBase::findItemAndNotifyUser(const QString& fileNameStr, const UtilityNamespace::ItemStatus status, UtilityNamespace::ItemTarget itemTarget) {

    for (int i = 0; i < this->nzbFileDataList.size(); i++) {

        NzbFileData nzbFileData = this->nzbFileDataList.at(i);

        // if nzbFileData has been identified :
        if (nzbFileData.match(fileNameStr)) {

            // used for 7z splitted files for eg
            // 7z splitted archive file is only the first field, set also the others one as archive files :
            nzbFileData.setArchiveFile(true);

            // update status for the corresponding nzbFileData :
            this->updateNzbFileDataInList(nzbFileData, status, i);

        }
    }

    // notify user of the current file being processed :
    this->emitProgressToArchivesWithCurrentStatus(status, itemTarget, this->extractProgressValue);
}



void ExtractBase::emitProgressToArchivesWithCurrentStatus(const UtilityNamespace::ItemStatus status, const UtilityNamespace::ItemTarget itemTarget,  const int percentage) {

    foreach (NzbFileData nzbFileData, this->nzbFileDataList) {

        if (nzbFileData.getExtractProgressionStep() == status) {
            // notify user of current file status and of its progression :
            emit updateExtractSignal(nzbFileData.getUniqueIdentifier(), percentage, status, itemTarget);
        }

    }

}



void ExtractBase::emitFinishToArchivesWithoutErrors(const UtilityNamespace::ItemStatus status, const int percentage) {

    foreach (NzbFileData nzbFileData, this->nzbFileDataList) {

        UtilityNamespace::ItemStatus nzbFileDataStatus = nzbFileData.getExtractProgressionStep();

        // look for files without extracting errors :
        if (nzbFileDataStatus != ExtractBadCrcStatus) {

            if (nzbFileDataStatus == ExtractStatus){
                emit updateExtractSignal(nzbFileData.getUniqueIdentifier(), percentage, status, ChildItemTarget);
            }
        }
        else {
            // only used to send *progression %* value for files with extracting errors :
            emit updateExtractSignal(nzbFileData.getUniqueIdentifier(), percentage, nzbFileData.getExtractProgressionStep(), ChildItemTarget);
        }

    }
}


void ExtractBase::emitStatusToAllArchives(const int& progress, const UtilityNamespace::ItemStatus status, const UtilityNamespace::ItemTarget target) {

    foreach (NzbFileData nzbFileData, this->nzbFileDataList) {

        if (nzbFileData.isArchiveFile()) {
            emit updateExtractSignal(nzbFileData.getUniqueIdentifier(), progress, status, target);
        }

    }

}

