#include <QDir>
#include <QDebug>

#include "global.h"
#include "ccorethread.h"
#include "floppyimagefactory.h"

BYTE        circBfr[2048];
int         cb_cnt, cb_posa, cb_posg;

#define		bfr_add(X)				{ circBfr[cb_posa] = X;     cb_posa++;      cb_posa &= 0x7FF;   cb_cnt++; }
#define		bfr_get(X)				{ X = circBfr[cb_posg];     cb_posg++;      cb_posg &= 0x7FF;   cb_cnt--; }

CCoreThread::CCoreThread()
{
    shouldRun       = true;
    running         = false;

    conUsb      = NULL;
    createConnectionObject();
    conUsb->tryToConnect();
}

CCoreThread::~CCoreThread()
{
    if(isRunning()) {
        stopRunning();
    }

    delete conUsb;
}

void CCoreThread::run(void)
{
    running = true;
    DWORD lastTick = GetTickCount();

    BYTE outBuff[2048], inBuff[2048];
    int side=0, track=0, sector=1;

    image = imageFactory.getImage((char *) "fcreated.msa");

    if(!image) {
        outDebugString("Image file type not supported!");
        return;
    }

    if(!image->isOpen()) {
        outDebugString("Image is not open!");
        return;
    }

//    BYTE buffer[512];
//    outDebugString("Check start");
//    for(int t=0; t<80; t++) {
//        for(int si=0; si<2; si++) {
//            for(int se=1; se<=9; se++) {
//                image->readSector(t, si, se, buffer);

//                if(buffer[0] != t || buffer[1] != si || buffer[2] != se) {
//                    outDebugString("Data mismatch: %d-%d-%d vs %d-%d-%d", t, si, se, buffer[0], buffer[1], buffer[2]);
//                }
//            }
//        }
//    }
//    outDebugString("Check end");

    bool checkFwOnce = true;

    while(shouldRun) {
        while(cb_cnt >= 8) {                               // while we have enough data
            int val;
            bfr_get(val);

            switch(val) {
                case ATN_FW_VERSION:
                    handleFwVersion();
                    break;
                case ATN_SEND_NEXT_SECTOR:
                    handleSendNextSector(side, track, sector, outBuff, inBuff);
                    break;
                case ATN_SECTOR_WRITTEN:
                    handleSectorWasWritten();
                    break;
            }
        }

        if(conUsb == NULL) {                            // if no connection object
            msleep(100);
            continue;
        }

        if(!conUsb->isConnected()) {                    // if not connected?
            conUsb->tryToConnect();                     // try to connect and wait

            msleep(100);
            continue;
        }

        if(GetTickCount() - lastTick < 10) {            // less than 10 ms ago?
            msleep(1);
            continue;
        }
        lastTick = GetTickCount();

        if(checkFwOnce) {                               // if we're connected, we should check FW version once
            outBuff[0] = CMD_GET_FW_VERSION;
            outBuff[1] = 0;

            sendAndReceive(2, outBuff, inBuff);
            checkFwOnce = false;
        }

        memset(outBuff, 0, 8);                          // send 8 zeros to receive any potential command
        sendAndReceive(8, outBuff, inBuff);
    }

    running = false;
}

void CCoreThread::sendAndReceive(int cnt, BYTE *outBuf, BYTE *inBuf)
{
    if(!conUsb->isConnected()) {                    // not connected? quit
        return;
    }

    conUsb->txRx(cnt, outBuf, inBuf);               // send and receive

    QString so,si;
    int val;

    for(int i=0; i<cnt; i++) {
        val = (int) outBuf[i];
        so = so + QString("%1 ").arg(val, 2, 16, QLatin1Char('0'));

        val = (int) inBuf[i];
        si = si + QString("%1 ").arg(val, 2, 16, QLatin1Char('0'));
    }
    qDebug() << "Sent: " << so;
    qDebug() << "Got : " << si;


    for(int i=0; i<cnt; i++) {                      // add to circular buffer
        bfr_add(inBuf[i]);
    }
}

void CCoreThread::handleFwVersion(void)
{
    BYTE fwVer[6];
    int val;

    for(int i=0; i<6; i++) {
        bfr_get(val);
        fwVer[i] = val;
    }

    if(fwVer[0] == 0xf0) {
        outDebugString("FW: Franz ");
    } else {
        outDebugString("FW: Hans ");
    }

    int year = bcdToInt(fwVer[1]) + 2000;
    outDebugString("%d-%02d-%02d", year, bcdToInt(fwVer[2]), bcdToInt(fwVer[3]));
}

int CCoreThread::bcdToInt(int bcd)
{
    int a,b;

    a = bcd >> 4;       // upper nibble
    b = bcd &  0x0f;    // lower nibble

    return ((a * 10) + b);
}

void CCoreThread::handleSendNextSector(int &side, int &track, int &sector, BYTE *oBuf, BYTE *iBuf)
{
    BYTE buf[6];
    int val;

    for(int i=0; i<6; i++) {        // get arguments from buffer
        bfr_get(val);
        buf[i] = val;
    }

    int count;
    side    = buf[0];               // now read the current floppy position
    track   = buf[1];
    sector  = buf[2];

    int tr, si, spt;
    image->getParams(tr, si, spt);  // read the floppy image params

    if(sector < spt) {              // if we're not yet at the end of the track
        sector++;                   // move to the next sector and send data to floppy
    } else {                        // if we're at the end of the track, don't send data and let the floppy starve
        return;
    }

    oBuf[0] = CMD_CURRENT_SECTOR;                           // first send the current sector #
    oBuf[1] = sector;

    createMfmStream(side, track, sector, oBuf + 2, count);  // then create the right MFM stream

    count += 2;                                             // now add the 2 bytes used by CMD_CURRENT_SECTOR

    if((count & 0x0001) != 0) {                     // if got even number of bytes
        oBuf[count] = 0;                            // store 0 at last position and increment count
        count++;
    }

    sendAndReceive(count, oBuf, iBuf);                // send and receive data
}

void CCoreThread::handleSectorWasWritten(void)
{


}

void CCoreThread::stopRunning(void)
{
    shouldRun = false;
}

bool CCoreThread::isRunning(void)
{
    return running;
}

void CCoreThread::createConnectionObject(void)
{
    if(!conUsb) {                       // create object
        conUsb = new CConUsb();
    }

    if(!conUsb->init()) {               // load dll, if failed, delete object
        outDebugString("Failed to open USB connection - .dll not loaded.");
    } else {
        outDebugString("USB connection ready to connect to device.");
    }
}

void CCoreThread::usbConnectionCheck(void)
{
    if(!conUsb) {                           // no usb connection object?
        return;
    }

    if(!conUsb->isConnected()) {            // if not connected, try to connect
        conUsb->tryToConnect();
    } else {                                // if connected, check if still connected
        if(!conUsb->connectionWorking()) {  // connection not working?
            createConnectionObject();
        }
    }
}

bool CCoreThread::createMfmStream(int side, int track, int sector, BYTE *buffer, int &count)
{
    bool res;

    count = 0;                                              // no data yet

    BYTE data[512];
    res = image->readSector(track, side, sector, buffer);

    if(!res) {
        return false;
    }

    int i;
    for(i=0; i<12; i++) {                                   // GAP 2: 12 * 0x00
        appendByteToStream(0, buffer, count);
    }

    WORD CRC = 0xffff;                                      // init CRC
    for(i=0; i<3; i++) {                                    // GAP 2: 3 * A1 mark
        appendA1MarkToStream(buffer, count);
        fdc_add_to_crc(CRC, 0xa1);
    }

    appendByteToStream( 0xfe,    buffer, count);            // ID record
    fdc_add_to_crc(CRC, 0xfe);

    appendByteToStream( track,   buffer, count);
    fdc_add_to_crc(CRC, track);

    appendByteToStream( side,    buffer, count);
    fdc_add_to_crc(CRC, side);

    appendByteToStream( sector,  buffer, count);
    fdc_add_to_crc(CRC, sector);

    appendByteToStream( 0x02,    buffer, count);            // size -- 2 == 512 B per sector
    fdc_add_to_crc(CRC, 0x02);

    appendByteToStream(HIBYTE(CRC), buffer, count);         // crc1
    appendByteToStream(LOBYTE(CRC), buffer, count);         // crc2

    for(i=0; i<22; i++) {                                   // GAP 3a: 22 * 0x4e
        appendByteToStream(0x4e, buffer, count);
    }

    for(i=0; i<12; i++) {                                   // GAP 3b: 12 * 0x00
        appendByteToStream(0, buffer, count);
    }

    CRC = 0xffff;                                           // init CRC
    for(i=0; i<3; i++) {                                    // GAP 3b: 3 * A1 mark
        appendA1MarkToStream(buffer, count);
        fdc_add_to_crc(CRC, 0xa1);
    }

    appendByteToStream( 0xfb, buffer, count);               // DAM mark
    fdc_add_to_crc(CRC, 0xfb);

    for(i=0; i<512; i++) {                                  // data
        appendByteToStream( data[i], buffer, count);
        fdc_add_to_crc(CRC, data[i]);
    }

    appendByteToStream(HIBYTE(CRC), buffer, count);         // crc1
    appendByteToStream(LOBYTE(CRC), buffer, count);         // crc2

    for(i=0; i<40; i++) {                                   // GAP 4: 40 * 0x4e
        appendByteToStream(0x4e, buffer, count);
    }

    return true;
}

void CCoreThread::appendByteToStream(BYTE val, BYTE *bfr, int &cnt)
{
    static BYTE prevBit = 0;

    for(int i=0; i<8; i++) {                        // for all bits
        BYTE bit = val & 0x80;                      // get highest bit
        val = val << 1;                             // shift up

        if(bit == 0) {                              // current bit is 0?
            if(prevBit == 0) {                      // append 0 after 0?
                appendChange(1, bfr, cnt);  // R
                appendChange(0, bfr, cnt);  // N
            } else {                                // append 0 after 1?
                appendChange(0, bfr, cnt);  // N
                appendChange(0, bfr, cnt);  // N
            }
        } else {                                    // current bit is 1?
            appendChange(0, bfr, cnt);              // N
            appendChange(1, bfr, cnt);              // R
        }

        prevBit = bit;                              // store this bit for next cycle
    }
}

void CCoreThread::appendChange(BYTE chg, BYTE *bfr, int &cnt)
{
    static BYTE changes = 0;

    changes = changes << 1;             // shift up
    changes = changes | chg;            // append change

    if(changes == 0 || changes == 1) {  // no 1 or single 1 found, quit
        return;
    }

    if(chg != 1) {                      // not adding 1 right now? quit
        return;
    }

    BYTE time = 0;

    switch(changes) {
    case 0x05:  time = MFM_4US; break;        // 4 us - stored as 1
    case 0x09:  time = MFM_6US; break;        // 6 us - stored as 2
    case 0x11:  time = MFM_8US; break;        // 8 us - stored as 3

    default:
        outDebugString("appendChange -- this shouldn't happen!");
        return;
    }

    changes = 0x01;                     // leave only lowest change

    appendTime(time, bfr, cnt);         // append this time to stream
}

void CCoreThread::appendTime(BYTE time, BYTE *bfr, int &cnt)
{
    static BYTE times       = 0;
    static BYTE timesCnt    = 0;

    times = times << 2;                 // shift 2 up
    times = times | time;               // add lowest 2 bits

    timesCnt++;                         // increment the count of times we have
    if(timesCnt == 4) {                 // we have 4 times (whole byte), store it
        timesCnt = 0;

        bfr[cnt] = times;               // store times
        cnt++;                          // increment counter of stored times
    }
}

void CCoreThread::appendA1MarkToStream(BYTE *bfr, int &cnt)
{
    // append A1 mark in stream, which is 8-6-8-6 in MFM (normaly would been 8-6-4-4-6)
    appendTime(MFM_8US, bfr, cnt);        // 8 us
    appendTime(MFM_6US, bfr, cnt);        // 6 us
    appendTime(MFM_8US, bfr, cnt);        // 8 us
    appendTime(MFM_6US, bfr, cnt);        // 6 us
}

void CCoreThread::fdc_add_to_crc(WORD &crc, BYTE data)
{
  for (int i=0;i<8;i++){
    crc = ((crc << 1) ^ ((((crc >> 8) ^ (data << i)) & 0x0080) ? 0x1021 : 0));
  }
}

void outDebugString(const char *format, ...)
{
    va_list args;
    va_start(args, format);

#define KJUT
#ifdef KJUT
    char tmp[1024];
    vsprintf(tmp, format, args);
    qDebug() << tmp;
#else
    vprintf(format, args);
#endif

    va_end(args);
}
