#ifndef CARBUSSERIAL_H
#define CARBUSSERIAL_H

#include <QSerialPort>
#include <QThread>
#include <QTimer>

#include "canconnection.h"

class CARBUSconnection : public QObject
{
    Q_OBJECT
public:

    CARBUSconnection(QThread &workingThread) {
        moveToThread(&workingThread);
        workingThread.start();
    }

    struct DeviceResponse {
        uchar       cmd {};
        uchar       channel {};
        QByteArray  data;

        operator bool() const
        { return cmd != 0x00; }
    };

public slots:
    void connectDevice(QString portName);
    void disconnectDevice();
    DeviceResponse sendCommand(uchar cmd, uchar channel, QByteArray cmdData, uint timeout);
signals:
    void sendDebug(QString dbg);
    void gotResponse(uchar cmd, uchar channel, QByteArray cmdData);
    void statusChanged(CANConStatus status, bool canFdSupport);

private:
    QSerialPort *serial {};
private slots:
    void readSerialData();
    void serialError(QSerialPort::SerialPortError err);

private:
    uchar           sequenceCnt {};
    QByteArray      rxBuf;
    QElapsedTimer   rxTimer;

    void txCommand(uchar cmd, uchar channel, QByteArray cmdData);

};


class CARBUSSerial : public CANConnection
{
    Q_OBJECT

public:
    CARBUSSerial(QString portName, int serialSpeed, int busSpeed, bool canFd, int dataRate);
    virtual ~CARBUSSerial();

protected:

    virtual void piStarted() override;
    virtual void piStop() override;
    virtual void piSetBusSettings(int pBusIdx, CANBus pBus) override;
    virtual bool piGetBusSettings(int pBusIdx, CANBus& pBus) override;
    virtual void piSuspend(bool pSuspend) override;
    virtual bool piSendFrame(const CANFrame& frame) override;

public slots:
    void debugInput(QByteArray bytes);

private slots:
    void connectionChanged(CANConStatus conStatus, bool canFdSupport);
    void deviceResponse(uchar cmd, uchar channel, QByteArray cmdData);
    void sendDebug(QString debugText);
signals:
    void connectDevice(QString portName);
    void disconnectDevice();
    void sendCmdToDevice(uchar cmd, uchar channel, QByteArray cmdData, uint timeout);

private:

    QThread            mCommThread;
    CARBUSconnection   mCommDevice;

    #pragma pack(push,1)
    struct PacketHeader {
        uint flags = 0;
        uint time = 0;      // used only for received messages
        uint crc = 0;       // to be used for LIN?
        uint msgId = 0;     // frame identifier
        ushort dlc = 0;     // for CAN-FD encoded according to the CAN-FD specification
    };
    static_assert(sizeof(PacketHeader)==18, "Invalid PacketHeader size");
    #pragma pack(pop)

};

#endif // CARBUSSERIAL_H
