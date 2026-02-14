#include <QDateTime>
#include <QDebug>

#include "interface.hpp"
#include "listener.hpp"

CandleApiInterface::CandleApiInterface(candle_handle handle, QString name) :
    _handle(handle),
    _name(name),
    channels(0),
    _lstn(0)
{
}

CandleApiInterface::~CandleApiInterface()
{
}

std::wstring CandleApiInterface::getPath() const
{
    return std::wstring(candle_dev_get_path(_handle));
}

QString CandleApiInterface::getName() const
{
    return _name;
}

candle_handle CandleApiInterface::getHandle() const
{
    return _handle;
}

bool CandleApiInterface::exists() const
{
    return candle_dev_exists(_handle);
}

bool CandleApiInterface::openChannel(uint8_t channel)
{
    if (!channels) {

        if (!candle_dev_open(_handle))
            return false;

        _timestampOffset = QDateTime::currentMSecsSinceEpoch() * 1000;
        _timestampLast = 0;
        uint32_t t_dev;
        if (candle_dev_get_timestamp_us(_handle, &t_dev)) {
            _timestampOffset -= t_dev;
        }
        else {
            qWarning() << "Candle: hw timestamps not supported?!";
        }
    }

    channels |= (1 << channel);

    return true;
}

bool CandleApiInterface::startChannel(uint8_t channel)
{
    uint32_t flags = 0;
    candle_channel_start(_handle, channel, flags);

    if (!_lstn) {

        qDebug() << "create Listener";
        _lstn = new CandleApiListener(*this);

        _lstn->startThread();
    }

    return true;
}

void CandleApiInterface::closeChannel(uint8_t channel)
{
    candle_channel_stop(_handle, channel);

    channels &= ~(1 << channel);

    if (!channels) {
        _lstn->stopThread();
        delete _lstn;
        _lstn = 0;
        qDebug() << "before candle_dev_close()";
        candle_dev_close(_handle);
        qDebug() << "before candle_dev_close()";
    }
}

bool CandleApiInterface::writeFrame(candle_frame_t & frame)
{
    return candle_frame_send(_handle, frame.channel, &frame);
}

void CandleApiInterface::readFrames()
{
    candle_frame_t frame;
    const unsigned int timeout_ms = 10;

    while (_lstn->_shouldBeRunning)
    {
        if (candle_frame_read(_handle, &frame, timeout_ms))
        {
            if (candle_frame_type(&frame) == CANDLE_FRAMETYPE_RECEIVE)
            {
                QCanBusFrame msg;

                if (candle_frame_is_rtr(&frame))
                {
                    msg.setFrameType(QCanBusFrame::RemoteRequestFrame);
                }
                else
                {
                    msg.setFrameType(QCanBusFrame::DataFrame);
                }
                if (candle_frame_is_extended_id(&frame))
                {
                    msg.setExtendedFrameFormat(true);
                }
                else
                {
                    msg.setExtendedFrameFormat(false);
                }
                msg.setFrameId(candle_frame_id(&frame));

                uint8_t dlc = candle_frame_dlc(&frame);
                QByteArray payload = QByteArray(reinterpret_cast<const char*>(candle_frame_data(&frame)), dlc);
                msg.setPayload(payload);

                uint64_t ts_us = _timestampOffset + candle_frame_timestamp_us(&frame);

                const uint64_t timestampGap = 5000; // allow 5 ms fluctuations from device
                if (_timestampLast > ts_us + timestampGap)
                {
                    //qDebug() << "Candle timestamp wrap" << _timestampLast << " vs " << ts_us;
                    // device timestamp had overflow (every 71 minutes)
                    ts_us += 1ull << 32;
                    _timestampOffset += 1ull << 32;
                }
                _timestampLast = ts_us;

                msg.setTimeStamp(QCanBusFrame::TimeStamp(0, ts_us));

                //qDebug() << "snd frame:" << msg.toString();
                emit sig_msg(frame.channel, msg);
            }
        }
    }
}

