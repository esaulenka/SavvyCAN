#ifndef CARBUSSERIAL_H
#define CARBUSSERIAL_H

#include <QSerialPort>
#include <QThread>
#include <QTimer>

#include "canconnection.h"


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

private:
    void sendDebug(QString debugText);

    // common communication part
    QSerialPort *serial {};
    Q_SLOT void serialError(QSerialPort::SerialPortError err);

    // receiving part
    uchar           sequenceCnt {};
    QByteArray      rxBuf;
    QElapsedTimer   rxTimer;
    Q_SLOT void readSerialData();

    Q_SIGNAL void gotResponse(uchar cmd, uchar channel, QByteArray cmdData);
    Q_SLOT void deviceResponse(uchar cmd, uchar channel, QByteArray cmdData);

    // sending part
    void txCommand(uchar cmd, uchar channel, QByteArray cmdData);

    struct DeviceResponse {
        uchar       cmd {};
        uchar       channel {};
        QByteArray  data;

        operator bool() const
        { return cmd != 0x00; }
    };
    DeviceResponse sendCommand(uchar cmd, uchar channel, QByteArray cmdData, uint timeout);


    // Some protocol details
    #pragma pack(push,1)
    struct PacketHeader {   // frame header
        uint flags = 0;
        uint time = 0;      // used only for received messages
        uint crc = 0;       // to be used for LIN?
        uint msgId = 0;     // frame identifier
        ushort dlc = 0;     // for CAN-FD encoded according to the CAN-FD specification
    };
    static_assert(sizeof(PacketHeader)==18, "Invalid PacketHeader size");
    #pragma pack(pop)

    enum : uchar {
        CARBUS_CH0 = 0x20,  // CAN1 (or LIN for LIN-only configuration)
        CARBUS_CH1 = 0x40,  // CAN2
        CARBUS_CH2 = 0x60,  // LIN for 2CAN+Lin devices
    };

};

#endif // CARBUSSERIAL_H
