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

#include "segmentdecoderbase.h"

#include "kwooty_debug.h"

#include <QDir>

#include "kwootysettings.h"
#include "data/segmentdata.h"
#include "segmentsdecoderthread.h"

SegmentDecoderBase::SegmentDecoderBase(SegmentsDecoderThread *parent) : QObject(parent)
{

    this->mSegmentsDecoderThread = parent;

}

QString SegmentDecoderBase::scanSegmentFiles(const NzbFileData &currentNzbFileData)
{

    this->mParentIdentifer = currentNzbFileData.getUniqueIdentifier();
    this->mSegmentDataList = currentNzbFileData.getSegmentList();

    QString fileName;

    //notify item that list of files is being scanned :
    if (!this->mSegmentDataList.isEmpty()) {

        PostDownloadInfoData decodeInfoData;
        decodeInfoData.initDecode(this->mParentIdentifer, PROGRESS_INIT, ScanStatus);
        this->decodeProgression(decodeInfoData);

    }

    // scan every files to be decoded :
    foreach (const SegmentData &currentSegment, this->mSegmentDataList) {

        // if segment has been downloaded :
        if (currentSegment.getArticlePresenceOnServer() == Present) {

            QString pathNameFile = Utility::buildFullPath(Settings::temporaryFolder().path(), currentSegment.getPart());
            QFile segmentFile(pathNameFile);

            // open the current segment file :
            segmentFile.open(QIODevice::ReadOnly);

            // look for file name :
            fileName = this->searchPattern(&segmentFile);

            // close the current segment file :
            segmentFile.close();
        }

        // file name has been found, stop scanning files :
        if (!fileName.isEmpty()) {
            break;
        }

    } // end of loop

    return fileName;

}

QString SegmentDecoderBase::scanCurrentSegment(SegmentData &currentSegment)
{

    QString fileName;

    // if segment has been downloaded :
    if (currentSegment.getArticlePresenceOnServer() == Present) {

        // open the current segment file :
        QIODevice *ioDevice = currentSegment.getIoDevice();
        ioDevice->open(QIODevice::ReadOnly);

        // look for file name :
        fileName = this->searchPattern(ioDevice);

        // close the current segment file :
        ioDevice->close();

    }

    return fileName;

}

