#include <QObject>
#include <QDebug>
#include <QCanBusFrame>
#include <QSerialPortInfo>
#include <QSettings>
#include <QStringBuilder>
#include <QEventLoop>

#include "carbus_serial.h"
#include "utility.h"


enum class CarBusDevice {
    /* Old identifier for CAN-Hacker on F105 mcu with dual CAN channels and single LIN channel */
    HW_CH30 = 0xFF,
    /* Old identifier for CAN-Hacker in ODB interface with single CAN channel and single LIN channel */
    HW_ODB_OLD = 0x02,
    /* CAN-Hacker 3.2 on F105 mcu with dual CAN channels and single LIN channel */
    HW_CH32 = 0x01,
    /* CAN-Hacker in ODB interface on F105 mcu with single CAN channel and single LIN channel */
    HW_ODB = 0x04,
    /* CAN-Hacker CH-P on F105 mcu with dual CAN channels and single LIN channel */
    HW_CHP = 0x03,
    /* CAN-Hacker 3.3 on F407 mcu with dual CAN channels and single LIN channel */
    HW_CH33 = 0x11,
    /* CAN-Hacker CH-P on F407 mcu with dual CAN channels and single LIN channel */
    HW_CHPM03 = 0x13,
    /* CAN-Hacker in ODB interface on G431 mcu with single CAN channel and single LIN channel */
    HW_ODB_FD = 0x14,
    /* CAN-Hacker CH-P on G473 mcu with dual CAN channels and single LIN channel */
    HW_FDL2 = 0x06,
};


CARBUSSerial::CARBUSSerial(QString portName, int serialSpeed, int busSpeed, bool canFd, int dataRate) :
    CANConnection(portName, "CARBUS", CANCon::CARBUS, serialSpeed, busSpeed, canFd, dataRate, 3, 4000, true)
{
    sendDebug("CARBUSSerial()");

    connect(this, &CARBUSSerial::gotResponse, this, &CARBUSSerial::deviceResponse);
}


CARBUSSerial::~CARBUSSerial()
{
    sendDebug("~CARBUSSerial() start");
    stop();
    sendDebug("~CARBUSSerial() end");
}

void CARBUSSerial::sendDebug(QString debugText)
{
    qDebug() << debugText;
    emit debugOutput(debugText);
}


void CARBUSSerial::piStarted()
{    
    QString portName = getPort();

    if (serial)
        piStop();

    // open new device
    sendDebug("Serial port: " + portName);

    serial = new QSerialPort(QSerialPortInfo(portName));
    if(!serial) {
        sendDebug("can't open serial port " + portName);
        return;
    }
    sendDebug("Created Serial Port Object");

    // connect reading event
    connect(serial, &QSerialPort::readyRead, this, &CARBUSSerial::readSerialData);
    connect(serial, &QSerialPort::errorOccurred, this, &CARBUSSerial::serialError);

    // configure
    serial->setBaudRate(115200);    // exact speed doesn't matter
    serial->setFlowControl(serial->NoFlowControl);
    if (!serial->open(QIODevice::ReadWrite))
    {
        sendDebug("Error returned during port opening: " + serial->errorString());
        return;
    }
    serial->setDataTerminalReady(true); //Seemingly these two lines used to be needed
    serial->setRequestToSend(true);     //But, really both ends should automatically handle these


    sendDebug("Connecting to CARBUS Device");

    DeviceResponse resp;
    // startup command:  a5 00 a5 00 -> 5a 00 5a 00
    sequenceCnt = -1;   // some trick: will be incremented to 0
    resp = sendCommand(0xA5, 0xA5, QByteArray{}, 1000);
    if (!resp) {
        sendDebug("Device not connected?");
        return;
    }

    // Get device name
    resp = sendCommand(0x01, 0x00, QByteArray{}, 100);
    if (!resp) {
        sendDebug("Invalid response?!");
        return;
    }
    sendDebug("Found device: " % QString(resp.data));

    // Get device type
    resp = sendCommand(0x05, 0x00, QByteArray{}, 100);
    if (!resp || resp.data.isEmpty()) {
        sendDebug("Invalid response?!");
        return;
    }
    int deviceType = resp.data[0];
    sendDebug(QString("Device type: %1").arg(deviceType, 2, 16));

    // Set device mode 'CAN only'
    resp = sendCommand(0x04, 0x01, QByteArray{}, 100);
    if (!resp) {
        sendDebug("Invalid response?!");
        return;
    }

    // Open device
    resp = sendCommand(0x08, 0x00, QByteArray{}, 100);
    if (!resp) {
        sendDebug("Invalid response?!");
        return;
    }

    CANConStatus conStatus {CANCon::CONNECTED, 1};
    bool canFd = false;
    switch (CarBusDevice(deviceType))
    {
    case CarBusDevice::HW_CH30:         // F105, 2 CAN + LIN
    case CarBusDevice::HW_CH32:         // F105, 2 CAN + LIN
    case CarBusDevice::HW_CHP:          // F105, 2 CAN + LIN
    case CarBusDevice::HW_CH33:         // F407, 2 CAN + LIN
    case CarBusDevice::HW_CHPM03:       // F407, 2 CAN + LIN
        conStatus.numHardwareBuses = 2;
        break;
    case CarBusDevice::HW_ODB_OLD:      // F105, 1 CAN + LIN
    case CarBusDevice::HW_ODB:          // F105, 1 CAN + LIN
        conStatus.numHardwareBuses = 1;
        break;
    case CarBusDevice::HW_ODB_FD:       // G431, 1 FDCAN + LIN
        conStatus.numHardwareBuses = 1;
        canFd = true;
        break;
    case CarBusDevice::HW_FDL2:         // G473, 2 FDCAN + LIN
        conStatus.numHardwareBuses = 1;
        canFd = true;
        break;
    }

    // Connected!
    setStatus(CANCon::CONNECTED);
    mNumBuses = conStatus.numHardwareBuses;
    mBusData.resize(mNumBuses);
    for (BusData & bus : mBusData)
    {
        bus.mConfigured = true;     // why this flag needed ?!
        bus.mBus = CANBus();
        bus.mBus.setCanFD(canFd);
    }

    emit status(conStatus);

}


void CARBUSSerial::piSuspend(bool pSuspend)
{
    /* update capSuspended */
    setCapSuspended(pSuspend);

    /* flush queue if we are suspended */
    if(isCapSuspended())
        getQueue().flush();
}


void CARBUSSerial::piStop()
{
    qDebug() << "CARBUSSerial::piStop()";

    if (serial != nullptr)
    {
        if (serial->isOpen())
        {
            // TODO close connection on device side
            serial->close();
        }
        serial->disconnect(); //disconnect all signals
        delete serial;
        serial = nullptr;
    }

    CANConStatus stats;
    stats.conStatus = CANCon::NOT_CONNECTED;
    stats.numHardwareBuses = 0;
    emit status(stats);
}


bool CARBUSSerial::piGetBusSettings(int pBusIdx, CANBus& pBus)
{
    return getBusConfig(pBusIdx, pBus);
}


void CARBUSSerial::piSetBusSettings(int pBusIdx, CANBus bus)
{
    if( (pBusIdx < 0) || pBusIdx >= getNumBuses())
        return;

    // copy bus config
    setBusConfig(pBusIdx, bus);

    CANBus & config = mBusData[pBusIdx].mBus;


    const uchar channel = (pBusIdx == 0) ? CARBUS_CH0 : CARBUS_CH1;

    // close channel
    sendCommand(0x19, channel, {}, 100);

    // set speed
    QByteArray speed("\0", 1);
    switch (config.getSpeed())
    {
    case 10000:     speed[0] = 0; break;
    case 20000:     speed[0] = 1; break;
    case 33333:     speed[0] = 2; break;
    case 50000:     speed[0] = 3; break;
    case 62500:     speed[0] = 4; break;
    case 83333:     speed[0] = 5; break;
    case 92500:     speed[0] = 6; break;
    case 100000:    speed[0] = 7; break;
    case 125000:    speed[0] = 8; break;
    case 250000:    speed[0] = 9; break;
    case 400000:    speed[0] = 10; break;
    case 500000:    speed[0] = 11; break;
    case 800000:    speed[0] = 12; break;
    case 1000000:   speed[0] = 13; break;
    }
    sendCommand(0x11, channel | 0x00, speed, 100);

    // set listen mode
    QByteArray listenMode("\0", 1);
    if (config.isListenOnly())
        listenMode[0] = 0x01;
    sendCommand(0x11, channel | 0x09, listenMode, 100);

    // TODO configure CAN-FD

    // open channel
    sendCommand(0x18, channel, {}, 100);

    config.setActive(true);
}


bool CARBUSSerial::piSendFrame(const CANFrame& frame)
{
    qDebug() << "Sending out carbus frame with id " << frame.frameId() << " on bus " << frame.bus;

    const uchar channel = (frame.bus == 0) ? CARBUS_CH0 : CARBUS_CH1;

    PacketHeader pktHeader {};

    if (frame.hasExtendedFrameFormat()) pktHeader.flags |= 0x01;
    // RTR bit - 0x02
    if (frame.hasFlexibleDataRateFormat()) pktHeader.flags |= 0x04;
    if (frame.hasBitrateSwitch()) pktHeader.flags |= 0x08;

    pktHeader.dlc = Utility::bytes_to_dlc_code(frame.payload().size());

    QByteArray pktData((char*)&pktHeader, sizeof(pktHeader));
    sendCommand(0x40, channel, pktData + frame.payload(), 0);

    return true;
}


//Debugging data sent from connection window. Inject it into Comm traffic.
void CARBUSSerial::debugInput(QByteArray bytes) {
    // sendToSerial(bytes);
    Q_UNUSED(bytes);
    sendDebug("Not implemented yet");
}

void CARBUSSerial::deviceResponse(uchar cmd, uchar channel, QByteArray cmdData)
{
    sendDebug(QString("Dev response: cmd=%1 channel=%2 data=%3").arg((int)cmd, 2, 16).arg((int)channel, 2, 16).arg(QString(cmdData.toHex())));

    if (cmd == 0x40)    // packet received
    {
        if (isCapSuspended())   // don't capture packets
            return;

        CANFrame frame;

        if (cmdData.size() < (int)sizeof(PacketHeader))
        {
            sendDebug("Received packet too short?!");
            return;
        }
        auto *pktHeader = (const PacketHeader*)cmdData.constData();

        frame.bus = (channel != 0x40) ? 0 : 1;
        frame.setFrameId(pktHeader->msgId);

        frame.setExtendedFrameFormat(pktHeader->flags & 0x01);
        if (pktHeader->flags & 0x02)
            frame.setFrameType(CANFrame::RemoteRequestFrame);
        else if (pktHeader->flags & 0x01000000)
            frame.setFrameType(CANFrame::ErrorFrame);
        frame.setFlexibleDataRateFormat(pktHeader->flags & 0x04);
        frame.setBitrateSwitch(pktHeader->flags & 0x08);

        frame.setPayload(cmdData.mid(sizeof(PacketHeader)));

        // get frame from queue
        CANFrame* frame_p = getQueue().get();
        if (frame_p)
        {
            // copy frame
            *frame_p = frame;
            checkTargettedFrame(frame);
            // enqueue frame
            getQueue().queue();
        }
    }
}


void CARBUSSerial::txCommand(uchar cmd, uchar channel, QByteArray cmdData)
{
    sequenceCnt++;

    // normal request/response structure: <cmd> <sequence no>      <channel> <data length>      <data ...>
    // packets send/receive structure:    <40>  <sequence no> <00> <channel> <data length> <00> <data ...>

    QByteArray cmdBuf;
    cmdBuf.append(cmd);
    cmdBuf.append(sequenceCnt);
    if (cmd == 0x40) cmdBuf.append('\0');
    cmdBuf.append(channel);
    cmdBuf.append(cmdData.size());
    if (cmd == 0x40) cmdBuf.append(cmdData.size() / 256);
    cmdBuf.append(cmdData);

    if (!serial || !serial->isOpen())
    {
        sendDebug("Error: Serial port closed");
        return;
    }

    sendDebug("Write to serial -> " + cmdBuf.toHex(' '));

    serial->write(cmdBuf);
}

void CARBUSSerial::readSerialData()
{
    if (!serial || !serial->isOpen())
    {
        sendDebug("Error: Serial port closed");
        return;
    }

    if (serial->bytesAvailable() == 0) return;

    if (!rxBuf.isEmpty() && rxTimer.hasExpired(100))
    {
        sendDebug("Inter-byte timeout during receive");
        rxBuf.clear();
    }

    rxTimer.start();
    rxBuf.append(serial->readAll());

    // try to parse
    while (rxBuf.size() >= 4)
    {
        uchar cmd = rxBuf[0] & 0x7F;    // common response = (cmd | 0x80)
        bool shortHeader = (cmd != 0x40);
        uchar sequence = rxBuf[1];
        uchar channel = rxBuf[2];
        int hdrLen = shortHeader ? 4 : 6;

        Q_UNUSED(sequence); // should be the same as in the command

        // second check for the long header
        if (rxBuf.size() < hdrLen) return;

        int dataLen = shortHeader ? rxBuf[3] : (rxBuf[4] | (rxBuf[5] << 8));


        if (rxBuf.size() < (hdrLen + dataLen)) return;

        sendDebug("Got from serial -> " + rxBuf.toHex(' '));

        QByteArray rxData = rxBuf.mid(hdrLen, dataLen);
        emit gotResponse(cmd, channel, rxData);

        // throw read data
        rxBuf = rxBuf.mid(hdrLen + dataLen);
    }
}

CARBUSSerial::DeviceResponse CARBUSSerial::sendCommand(uchar cmd, uchar channel, QByteArray cmdData, uint timeout)
{
    // do not wait for answer
    if (timeout == 0)
    {
        txCommand(cmd, channel, cmdData);
        return {};
    }

    DeviceResponse resp;
    QEventLoop loop;

    auto msgParser = [&](uchar rxCmd, uchar rxChannel, QByteArray rxData)
    {
        sendDebug("sendCommand got resp=" + QString::number(cmd, 16));
        if (rxCmd == cmd ||
            rxCmd == 0x5A)  // special case, startup response
        {
            resp.cmd = rxCmd;
            resp.channel = rxChannel;
            resp.data = rxData;
            loop.quit();
        }
    };
    auto conn = connect(this, &CARBUSSerial::gotResponse, this, msgParser);

    QTimer::singleShot(timeout, &loop, &QEventLoop::quit);
    txCommand(cmd, channel, cmdData);

    loop.exec();
    disconnect(conn);

    return resp;
}


void CARBUSSerial::serialError(QSerialPort::SerialPortError err)
{
    QString errMessage;
    bool killConnection = false;
    switch (err)
    {
    case QSerialPort::NoError:
        return;
    case QSerialPort::DeviceNotFoundError:
        errMessage = "Device not found error on serial";
        killConnection = true;
        break;
    case QSerialPort::PermissionError:
        errMessage =  "Permission error on serial port";
        killConnection = true;
        break;
    case QSerialPort::OpenError:
        errMessage =  "Open error on serial port";
        killConnection = true;
        break;
    case QSerialPort::ParityError:
        errMessage = "Parity error on serial port";
        break;
    case QSerialPort::FramingError:
        errMessage = "Framing error on serial port";
        break;
    case QSerialPort::BreakConditionError:
        errMessage = "Break error on serial port";
        break;
    case QSerialPort::WriteError:
        errMessage = "Write error on serial port";
        break;
    case QSerialPort::ReadError:
        errMessage = "Read error on serial port";
        break;
    case QSerialPort::ResourceError:
        errMessage = "Serial port seems to have disappeared.";
        killConnection = true;
        break;
    case QSerialPort::UnsupportedOperationError:
        errMessage = "Unsupported operation on serial port";
        killConnection = true;
        break;
    case QSerialPort::UnknownError:
        errMessage = "Beats me what happened to the serial port.";
        killConnection = true;
        break;
    case QSerialPort::TimeoutError:
        errMessage = "Timeout error on serial port";
        killConnection = true;
        break;
    case QSerialPort::NotOpenError:
        errMessage = "The serial port isn't open";
        killConnection = true;
        break;
    }
    /*
    if (serial)
    {
        serial->clearError();
        serial->flush();
        serial->close();
    }*/
    if (!errMessage.isEmpty())
    {
        sendDebug(errMessage);
    }
    if (killConnection)
    {
        qDebug() << "Shooting the serial object in the head. It deserves it.";
        piStop();
    }
}
