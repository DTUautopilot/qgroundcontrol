/*===================================================================
======================================================================*/

/**
 * @file
 *   @brief Implementation of class MAVLinkProtocol
 *   @author Lorenz Meier <mail@qgroundcontrol.org>
 */

#include <inttypes.h>
#include <iostream>

#include <QDebug>
#include <QTime>
#include <QApplication>
#include <QMessageBox>
#include <QSettings>
#include <QDesktopServices>

//#include "MG.h"
#include "MAVLinkProtocol.h"
#include "UASInterface.h"
#include "UASManager.h"
#include "UASInterface.h"
#include "UAS.h"
#include "SlugsMAV.h"
#include "PxQuadMAV.h"
#include "ArduPilotMegaMAV.h"
#include "configuration.h"
#include "LinkManager.h"
//#include "MainWindow.h"
#include "QGCMAVLink.h"
#include "QGCMAVLinkUASFactory.h"
#include "QGC.h"

/**
 * The default constructor will create a new MAVLink object sending heartbeats at
 * the MAVLINK_HEARTBEAT_DEFAULT_RATE to all connected links.
 */
MAVLinkProtocol::MAVLinkProtocol() :
        heartbeatTimer(new QTimer(this)),
        heartbeatRate(MAVLINK_HEARTBEAT_DEFAULT_RATE),
        m_heartbeatsEnabled(false),
        m_loggingEnabled(false),
        m_logfile(NULL),
        m_enable_version_check(true),
        m_paramRetransmissionTimeout(350),
        m_paramRewriteTimeout(500),
        m_paramGuardEnabled(true),
        m_actionGuardEnabled(false),
        m_actionRetransmissionTimeout(100),
        versionMismatchIgnore(false),
        systemId(QGC::defaultSystemId)
{
    loadSettings();
    //start(QThread::LowPriority);
    // Start heartbeat timer, emitting a heartbeat at the configured rate
    connect(heartbeatTimer, SIGNAL(timeout()), this, SLOT(sendHeartbeat()));
    heartbeatTimer->start(1000/heartbeatRate);
    totalReceiveCounter = 0;
    totalLossCounter = 0;
    currReceiveCounter = 0;
    currLossCounter = 0;
    for (int i = 0; i < 256; i++)
    {
        for (int j = 0; j < 256; j++)
        {
            lastIndex[i][j] = -1;
        }
    }

    emit versionCheckChanged(m_enable_version_check);
}

void MAVLinkProtocol::loadSettings()
{
    // Load defaults from settings
    QSettings settings;
    settings.sync();
    settings.beginGroup("QGC_MAVLINK_PROTOCOL");
    enableHeartbeats(settings.value("HEARTBEATS_ENABLED", m_heartbeatsEnabled).toBool());
    enableVersionCheck(settings.value("VERSION_CHECK_ENABLED", m_enable_version_check).toBool());
    enableMultiplexing(settings.value("MULTIPLEXING_ENABLED", m_multiplexingEnabled).toBool());

    // Only set logfile if there is a name present in settings
    if (settings.contains("LOGFILE_NAME") && m_logfile == NULL)
    {
        m_logfile = new QFile(settings.value("LOGFILE_NAME").toString());
    }
    else if (m_logfile == NULL)
    {
        m_logfile = new QFile(QDesktopServices::storageLocation(QDesktopServices::HomeLocation) + "/qgroundcontrol_packetlog.mavlink");
    }
    // Enable logging
    enableLogging(settings.value("LOGGING_ENABLED", m_loggingEnabled).toBool());

    // Only set system id if it was valid
    int temp = settings.value("GCS_SYSTEM_ID", systemId).toInt();
    if (temp > 0 && temp < 256)
    {
        systemId = temp;
    }

    // Parameter interface settings
    bool ok;
    temp = settings.value("PARAMETER_RETRANSMISSION_TIMEOUT", m_paramRetransmissionTimeout).toInt(&ok);
    if (ok) m_paramRetransmissionTimeout = temp;
    temp = settings.value("PARAMETER_REWRITE_TIMEOUT", m_paramRewriteTimeout).toInt(&ok);
    if (ok) m_paramRewriteTimeout = temp;
    m_paramGuardEnabled = settings.value("PARAMETER_TRANSMISSION_GUARD_ENABLED", m_paramGuardEnabled).toBool();
    settings.endGroup();
}

void MAVLinkProtocol::storeSettings()
{
    // Store settings
    QSettings settings;
    settings.beginGroup("QGC_MAVLINK_PROTOCOL");
    settings.setValue("HEARTBEATS_ENABLED", m_heartbeatsEnabled);
    settings.setValue("LOGGING_ENABLED", m_loggingEnabled);
    settings.setValue("VERSION_CHECK_ENABLED", m_enable_version_check);
    settings.setValue("MULTIPLEXING_ENABLED", m_multiplexingEnabled);
    settings.setValue("GCS_SYSTEM_ID", systemId);
    if (m_logfile)
    {
        // Logfile exists, store the name
        settings.setValue("LOGFILE_NAME", m_logfile->fileName());
    }
    // Parameter interface settings
    settings.setValue("PARAMETER_RETRANSMISSION_TIMEOUT", m_paramRetransmissionTimeout);
    settings.setValue("PARAMETER_REWRITE_TIMEOUT", m_paramRewriteTimeout);
    settings.setValue("PARAMETER_TRANSMISSION_GUARD_ENABLED", m_paramGuardEnabled);
    settings.endGroup();
    settings.sync();
    //qDebug() << "Storing settings!";
}

MAVLinkProtocol::~MAVLinkProtocol()
{
    storeSettings();
    if (m_logfile)
    {
        if (m_logfile->isOpen())
        {
            m_logfile->flush();
            m_logfile->close();
        }
        delete m_logfile;
    }
}



void MAVLinkProtocol::run()
{
    exec();
}

QString MAVLinkProtocol::getLogfileName()
{
    if (m_logfile)
    {
        return m_logfile->fileName();
    }
    else
    {
        return QDesktopServices::storageLocation(QDesktopServices::HomeLocation) + "/qgroundcontrol_packetlog.mavlink";
    }
}

/**
 * The bytes are copied by calling the LinkInterface::readBytes() method.
 * This method parses all incoming bytes and constructs a MAVLink packet.
 * It can handle multiple links in parallel, as each link has it's own buffer/
 * parsing state machine.
 * @param link The interface to read from
 * @see LinkInterface
 **/
void MAVLinkProtocol::receiveBytes(LinkInterface* link, QByteArray b)
{
    receiveMutex.lock();
    mavlink_message_t message;
    mavlink_status_t status;
    for (int position = 0; position < b.size(); position++)
    {
        unsigned int decodeState = mavlink_parse_char(link->getId(), (uint8_t)(b.at(position)), &message, &status);

        if (decodeState == 1)
        {
            // Log data
            if (m_loggingEnabled && m_logfile)
            {
                const int len = MAVLINK_MAX_PACKET_LEN+sizeof(quint64);
                uint8_t buf[len];
                quint64 time = QGC::groundTimeUsecs();
                memcpy(buf, (void*)&time, sizeof(quint64));
                // Write message to buffer
                mavlink_msg_to_send_buffer(buf+sizeof(quint64), &message);
                QByteArray b((const char*)buf, len);
                if(m_logfile->write(b) < static_cast<qint64>(MAVLINK_MAX_PACKET_LEN+sizeof(quint64)))
                {
                    emit protocolStatusMessage(tr("MAVLink Logging failed"), tr("Could not write to file %1, disabling logging.").arg(m_logfile->fileName()));
                    // Stop logging
                    enableLogging(false);
                }
            }

            // ORDER MATTERS HERE!
            // If the matching UAS object does not yet exist, it has to be created
            // before emitting the packetReceived signal
            UASInterface* uas = UASManager::instance()->getUASForId(message.sysid);

            // Check and (if necessary) create UAS object
            if (uas == NULL && message.msgid == MAVLINK_MSG_ID_HEARTBEAT)
            {
                // ORDER MATTERS HERE!
                // The UAS object has first to be created and connected,
                // only then the rest of the application can be made aware
                // of its existence, as it only then can send and receive
                // it's first messages.

                // Check if the UAS has the same id like this system
                if (message.sysid == getSystemId())
                {
                    emit protocolStatusMessage(tr("SYSTEM ID CONFLICT!"), tr("Warning: A second system is using the same system id (%1)").arg(getSystemId()));
                }

                // Create a new UAS based on the heartbeat received
                // Todo dynamically load plugin at run-time for MAV
                // WIKISEARCH:AUTOPILOT_TYPE_INSTANTIATION

                // First create new UAS object
                // Decode heartbeat message
                mavlink_heartbeat_t heartbeat;
                // Reset version field to 0
                heartbeat.mavlink_version = 0;
                mavlink_msg_heartbeat_decode(&message, &heartbeat);

                // Check if the UAS has a different protocol version
                if (m_enable_version_check && (heartbeat.mavlink_version != MAVLINK_VERSION))
                {
                    // Bring up dialog to inform user
                    if (!versionMismatchIgnore)
                    {
                        emit protocolStatusMessage(tr("The MAVLink protocol version on the MAV and QGroundControl mismatch!"),
                                                   tr("It is unsafe to use different MAVLink versions. QGroundControl therefore refuses to connect to system %1, which sends MAVLink version %2 (QGroundControl uses version %3).").arg(message.sysid).arg(heartbeat.mavlink_version).arg(MAVLINK_VERSION));
                        versionMismatchIgnore = true;
                    }

                    // Ignore this message and continue gracefully
                    continue;
                }

                // Create a new UAS object
                uas = QGCMAVLinkUASFactory::createUAS(this, link, message.sysid, &heartbeat);
            }

            // Only count message if UAS exists for this message
            if (uas != NULL)
            {
                // Increase receive counter
                totalReceiveCounter++;
                currReceiveCounter++;
                qint64 lastLoss = totalLossCounter;
                // Update last packet index
                if (lastIndex[message.sysid][message.compid] == -1)
                {
                    lastIndex[message.sysid][message.compid] = message.seq;
                }
                else
                {
                    // TODO: This if-else block can (should) be greatly simplified
                    if (lastIndex[message.sysid][message.compid] == 255)
                    {
                        lastIndex[message.sysid][message.compid] = 0;
                    }
                    else
                    {
                        lastIndex[message.sysid][message.compid]++;
                    }

                    int safeguard = 0;
                    //qDebug() << "SYSID" << message.sysid << "COMPID" << message.compid << "MSGID" << message.msgid << "LASTINDEX" << lastIndex[message.sysid][message.compid] << "SEQ" << message.seq;
                    while(lastIndex[message.sysid][message.compid] != message.seq && safeguard < 255)
                    {
                        if (lastIndex[message.sysid][message.compid] == 255)
                        {
                            lastIndex[message.sysid][message.compid] = 0;
                        }
                        else
                        {
                            lastIndex[message.sysid][message.compid]++;
                        }
                        totalLossCounter++;
                        currLossCounter++;
                        safeguard++;
                    }
                }
                //            if (lastIndex.contains(message.sysid))
                //            {
                //                QMap<int, int>* lastCompIndex = lastIndex.value(message.sysid);
                //                if (lastCompIndex->contains(message.compid))
                //                while (lastCompIndex->value(message.compid, 0)+1 )
                //            }
                //if ()

                // If a new loss was detected or we just hit one 128th packet step
                if (lastLoss != totalLossCounter || (totalReceiveCounter % 64 == 0))
                {
                    // Calculate new loss ratio
                    // Receive loss
                    float receiveLoss = (double)currLossCounter/(double)(currReceiveCounter+currLossCounter);
                    receiveLoss *= 100.0f;
                    // qDebug() << "LOSSCHANGED" << receiveLoss;
                    currLossCounter = 0;
                    currReceiveCounter = 0;
                    emit receiveLossChanged(message.sysid, receiveLoss);
                }

                // The packet is emitted as a whole, as it is only 255 - 261 bytes short
                // kind of inefficient, but no issue for a groundstation pc.
                // It buys as reentrancy for the whole code over all threads
                emit messageReceived(link, message);

                // Multiplex message if enabled
                if (m_multiplexingEnabled)
                {
                    // Get all links connected to this unit
                    QList<LinkInterface*> links = LinkManager::instance()->getLinksForProtocol(this);

                    // Emit message on all links that are currently connected
                    foreach (LinkInterface* currLink, links)
                    {
                        // Only forward this message to the other links,
                        // not the link the message was received on
                        if (currLink != link) sendMessage(currLink, message);
                    }
                }
            }
        }
    }
    receiveMutex.unlock();
}

/**
 * @return The name of this protocol
 **/
QString MAVLinkProtocol::getName()
{
    return QString(tr("MAVLink protocol"));
}

/** @return System id of this application */
int MAVLinkProtocol::getSystemId()
{
    return systemId;
}

void MAVLinkProtocol::setSystemId(int id)
{
    systemId = id;
}

/** @return Component id of this application */
int MAVLinkProtocol::getComponentId()
{
    return QGC::defaultComponentId;
}

/**
 * @param message message to send
 */
void MAVLinkProtocol::sendMessage(mavlink_message_t message)
{
    // Get all links connected to this unit
    QList<LinkInterface*> links = LinkManager::instance()->getLinksForProtocol(this);

    // Emit message on all links that are currently connected
    QList<LinkInterface*>::iterator i;
    for (i = links.begin(); i != links.end(); ++i)
    {
        sendMessage(*i, message);
        //qDebug() << __FILE__ << __LINE__ << "SENT MESSAGE OVER" << ((LinkInterface*)*i)->getName() << "LIST SIZE:" << links.size();
    }
}

/**
 * @param link the link to send the message over
 * @param message message to send
 */
void MAVLinkProtocol::sendMessage(LinkInterface* link, mavlink_message_t message)
{
    // Create buffer
    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
    // Rewriting header to ensure correct link ID is set
    if (link->getId() != 0) mavlink_finalize_message_chan(&message, this->getSystemId(), this->getComponentId(), link->getId(), message.len);
    // Write message into buffer, prepending start sign
    int len = mavlink_msg_to_send_buffer(buffer, &message);
    // If link is connected
    if (link->isConnected())
    {
        // Send the portion of the buffer now occupied by the message
        link->writeBytes((const char*)buffer, len);
    }
}

/**
 * The heartbeat is sent out of order and does not reset the
 * periodic heartbeat emission. It will be just sent in addition.
 * @return mavlink_message_t heartbeat message sent on serial link
 */
void MAVLinkProtocol::sendHeartbeat()
{
    if (m_heartbeatsEnabled)
    {
        mavlink_message_t beat;
        mavlink_msg_heartbeat_pack(getSystemId(), getComponentId(),&beat, OCU, MAV_AUTOPILOT_GENERIC);
        sendMessage(beat);
    }
}

/** @param enabled true to enable heartbeats emission at heartbeatRate, false to disable */
void MAVLinkProtocol::enableHeartbeats(bool enabled)
{
    m_heartbeatsEnabled = enabled;
    emit heartbeatChanged(enabled);
}

void MAVLinkProtocol::enableMultiplexing(bool enabled)
{
    bool changed = false;
    if (enabled != m_multiplexingEnabled) changed = true;

    m_multiplexingEnabled = enabled;
    if (changed) emit multiplexingChanged(m_multiplexingEnabled);
}

void MAVLinkProtocol::enableParamGuard(bool enabled)
{
    if (enabled != m_paramGuardEnabled)
    {
        m_paramGuardEnabled = enabled;
        emit paramGuardChanged(m_paramGuardEnabled);
    }
}

void MAVLinkProtocol::enableActionGuard(bool enabled)
{
    if (enabled != m_actionGuardEnabled)
    {
        m_actionGuardEnabled = enabled;
        emit actionGuardChanged(m_actionGuardEnabled);
    }
}

void MAVLinkProtocol::setParamRetransmissionTimeout(int ms)
{
    if (ms != m_paramRetransmissionTimeout)
    {
        m_paramRetransmissionTimeout = ms;
        emit paramRetransmissionTimeoutChanged(m_paramRetransmissionTimeout);
    }
}

void MAVLinkProtocol::setParamRewriteTimeout(int ms)
{
    if (ms != m_paramRewriteTimeout)
    {
        m_paramRewriteTimeout = ms;
        emit paramRewriteTimeoutChanged(m_paramRewriteTimeout);
    }
}

void MAVLinkProtocol::setActionRetransmissionTimeout(int ms)
{
    if (ms != m_actionRetransmissionTimeout)
    {
        m_actionRetransmissionTimeout = ms;
        emit actionRetransmissionTimeoutChanged(m_actionRetransmissionTimeout);
    }
}

void MAVLinkProtocol::enableLogging(bool enabled)
{
    bool changed = false;
    if (enabled != m_loggingEnabled) changed = true;

    if (enabled)
    {
        if (m_logfile && m_logfile->isOpen())
        {
            m_logfile->flush();
            m_logfile->close();
        }
        if (m_logfile)
        {
            if (!m_logfile->open(QIODevice::WriteOnly | QIODevice::Append))
            {
                emit protocolStatusMessage(tr("Opening MAVLink logfile for writing failed"), tr("MAVLink cannot log to the file %1, please choose a different file. Stopping logging.").arg(m_logfile->fileName()));
                m_loggingEnabled = false;
            }
        }
    }
    else if (!enabled)
    {
        if (m_logfile)
        {
            if (m_logfile->isOpen())
            {
                m_logfile->flush();
                m_logfile->close();
            }
            delete m_logfile;
            m_logfile = NULL;
        }
    }
    m_loggingEnabled = enabled;
    if (changed) emit loggingChanged(enabled);
}

void MAVLinkProtocol::setLogfileName(const QString& filename)
{
    if (!m_logfile)
    {
        m_logfile = new QFile(filename);
    }
    else
    {
        m_logfile->flush();
        m_logfile->close();
    }
    m_logfile->setFileName(filename);
    enableLogging(m_loggingEnabled);
}

void MAVLinkProtocol::enableVersionCheck(bool enabled)
{
    m_enable_version_check = enabled;
    emit versionCheckChanged(enabled);
}

/**
 * The default rate is 1 Hertz.
 *
 * @param rate heartbeat rate in hertz (times per second)
 */
void MAVLinkProtocol::setHeartbeatRate(int rate)
{
    heartbeatRate = rate;
    heartbeatTimer->setInterval(1000/heartbeatRate);
}

/** @return heartbeat rate in Hertz */
int MAVLinkProtocol::getHeartbeatRate()
{
    return heartbeatRate;
}
