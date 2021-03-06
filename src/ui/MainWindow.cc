/*===================================================================
======================================================================*/

/**
 * @file
 *   @brief Implementation of class MainWindow
 *   @author Lorenz Meier <mail@qgroundcontrol.org>
 */

#include <QSettings>
#include <QDockWidget>
#include <QNetworkInterface>
#include <QMessageBox>
#include <QDebug>
#include <QTimer>
#include <QHostInfo>

#include "MG.h"
#include "QGC.h"
#include "MAVLinkSimulationLink.h"
#include "SerialLink.h"
#include "UDPLink.h"
#include "MAVLinkProtocol.h"
#include "CommConfigurationWindow.h"
#include "QGCWaypointListMulti.h"
#include "MainWindow.h"
#include "JoystickWidget.h"
#include "GAudioOutput.h"
#include "QGCToolWidget.h"
#include "QGCMAVLinkLogPlayer.h"
#include "QGCSettingsWidget.h"

#ifdef QGC_OSG_ENABLED
#include "Q3DWidgetFactory.h"
#endif

// FIXME Move
#include "PxQuadMAV.h"
#include "SlugsMAV.h"


#include "LogCompressor.h"

MainWindow* MainWindow::instance()
{
    static MainWindow* _instance = 0;
    if(_instance == 0)
    {
        _instance = new MainWindow();

        /* Set the application as parent to ensure that this object
                 * will be destroyed when the main application exits */
        //_instance->setParent(qApp);
    }
    return _instance;
}

/**
* Create new mainwindow. The constructor instantiates all parts of the user
* interface. It does NOT show the mainwindow. To display it, call the show()
* method.
*
* @see QMainWindow::show()
**/
MainWindow::MainWindow(QWidget *parent):
        QMainWindow(parent),
        toolsMenuActions(),
        currentView(VIEW_UNCONNECTED),
        aboutToCloseFlag(false),
        changingViewsFlag(false),
        styleFileName(QCoreApplication::applicationDirPath() + "/style-indoor.css"),
        autoReconnect(false),
        currentStyle(QGC_MAINWINDOW_STYLE_INDOOR)
{
    loadSettings();
    if (!settings.contains("CURRENT_VIEW"))
    {
        // Set this view as default view
        settings.setValue("CURRENT_VIEW", currentView);
    }
    else
    {
        // LOAD THE LAST VIEW
        VIEW_SECTIONS currentViewCandidate = (VIEW_SECTIONS) settings.value("CURRENT_VIEW", currentView).toInt();
        if (currentViewCandidate != VIEW_ENGINEER &&
            currentViewCandidate != VIEW_OPERATOR &&
            currentViewCandidate != VIEW_PILOT)
        {
            currentView = currentViewCandidate;
        }
    }

    setDefaultSettingsForAp();

    settings.sync();

    // Setup user interface
    ui.setupUi(this);

    setVisible(false);

    buildCommonWidgets();

    connectCommonWidgets();

    arrangeCommonCenterStack();

    configureWindowName();

    loadStyle(currentStyle);

//    // Set the application style (not the same as a style sheet)
//    // Set the style to Plastique
//    qApp->setStyle("plastique");

//    // Set style sheet as last step
//    QFile* styleSheet = new QFile(":/images/style-mission.css");
//    if (styleSheet->open(QIODevice::ReadOnly | QIODevice::Text))
//    {
//        QString style = QString(styleSheet->readAll());
//        style.replace("ICONDIR", QCoreApplication::applicationDirPath()+ "/images/");
//        qApp->setStyleSheet(style);
//    }

    // Create actions
    connectCommonActions();

    // Set dock options
    setDockOptions(AnimatedDocks | AllowTabbedDocks | AllowNestedDocks);

    // Load mavlink view as default widget set
    //loadMAVLinkView();

    statusBar()->setSizeGripEnabled(true);

    // Restore the window position and size
    if (settings.contains(getWindowGeometryKey()))
    {
        // Restore the window geometry
        restoreGeometry(settings.value(getWindowGeometryKey()).toByteArray());
    }
    else
    {
        // Adjust the size
        adjustSize();
    }

    // Populate link menu
    QList<LinkInterface*> links = LinkManager::instance()->getLinks();
    foreach(LinkInterface* link, links)
    {
        this->addLink(link);
    }

    connect(LinkManager::instance(), SIGNAL(newLink(LinkInterface*)), this, SLOT(addLink(LinkInterface*)));

    // Connect user interface devices
    if (!joystick)
    {
        joystick = new JoystickInput();
    }

    // Enable and update view
    presentView();

    // Connect link
    if (autoReconnect)
    {
        SerialLink* link = new SerialLink();
        // Add to registry
        LinkManager::instance()->add(link);
        LinkManager::instance()->addProtocol(link, mavlink);
        link->connect();
    }
}

MainWindow::~MainWindow()
{
    // Store settings
    storeSettings();

    delete mavlink;
    delete joystick;

    // Get and delete all dockwidgets and contained
    // widgets
    QObjectList childList( this->children() );

    QObjectList::iterator i;
    QDockWidget* dockWidget;
    for (i = childList.begin(); i != childList.end(); ++i)
    {
        dockWidget = dynamic_cast<QDockWidget*>(*i);
        if (dockWidget)
        {
            // Remove dock widget from main window
            removeDockWidget(dockWidget);
            delete dockWidget->widget();
            delete dockWidget;
        }
    }
}

/**
 * Set default settings for this AP type.
 */
void MainWindow::setDefaultSettingsForAp()
{
    // Check if the settings exist, instantiate defaults if necessary

    // UNCONNECTED VIEW DEFAULT
    QString centralKey = buildMenuKey(SUB_SECTION_CHECKED, CENTRAL_MAP, VIEW_UNCONNECTED);
    if (!settings.contains(centralKey))
    {
        settings.setValue(centralKey,true);

        // ENABLE UAS LIST
        settings.setValue(buildMenuKey(SUB_SECTION_CHECKED,MainWindow::MENU_UAS_LIST, VIEW_UNCONNECTED), true);
        // ENABLE COMMUNICATION CONSOLE
        settings.setValue(buildMenuKey(SUB_SECTION_CHECKED,MainWindow::MENU_DEBUG_CONSOLE, VIEW_UNCONNECTED), true);
    }

    // OPERATOR VIEW DEFAULT
    centralKey = buildMenuKey(SUB_SECTION_CHECKED, CENTRAL_MAP, VIEW_OPERATOR);
    if (!settings.contains(centralKey))
    {
        settings.setValue(centralKey,true);

        // ENABLE UAS LIST
        settings.setValue(buildMenuKey(SUB_SECTION_CHECKED,MainWindow::MENU_UAS_LIST,VIEW_OPERATOR), true);
        // ENABLE HUD TOOL WIDGET
        settings.setValue(buildMenuKey(SUB_SECTION_CHECKED,MainWindow::MENU_HUD,VIEW_OPERATOR), true);
        // ENABLE WAYPOINTS
        settings.setValue(buildMenuKey(SUB_SECTION_CHECKED,MainWindow::MENU_WAYPOINTS,VIEW_OPERATOR), true);
    }

    // ENGINEER VIEW DEFAULT
    centralKey = buildMenuKey(SUB_SECTION_CHECKED, CENTRAL_LINECHART, VIEW_ENGINEER);
    if (!settings.contains(centralKey))
    {
        settings.setValue(centralKey,true);
        // Enable Parameter widget
        settings.setValue(buildMenuKey(SUB_SECTION_CHECKED,MainWindow::MENU_PARAMETERS,VIEW_ENGINEER), true);
    }

    // MAVLINK VIEW DEFAULT
    centralKey = buildMenuKey(SUB_SECTION_CHECKED, CENTRAL_PROTOCOL, VIEW_MAVLINK);
    if (!settings.contains(centralKey))
    {
        settings.setValue(centralKey,true);
    }

    // PILOT VIEW DEFAULT
    centralKey = buildMenuKey(SUB_SECTION_CHECKED, CENTRAL_HUD, VIEW_PILOT);
    if (!settings.contains(centralKey))
    {
        settings.setValue(centralKey,true);
        // Enable Flight display
        settings.setValue(buildMenuKey(SUB_SECTION_CHECKED,MainWindow::MENU_HDD_1,VIEW_PILOT), true);
    }
}

void MainWindow::resizeEvent(QResizeEvent * event)
{
    Q_UNUSED(event);
    if (height() < 800)
    {
        ui.statusBar->setVisible(false);
    }
    else
    {
        ui.statusBar->setVisible(true);
    }
}

QString MainWindow::getWindowStateKey()
{
    return QString::number(currentView)+"_windowstate";
}

QString MainWindow::getWindowGeometryKey()
{
    //return QString::number(currentView)+"_geometry";
    return "_geometry";
}

void MainWindow::buildCustomWidget()
{
    // Show custom widgets only if UAS is connected
    if (UASManager::instance()->getActiveUAS() != NULL)
    {
        // Enable custom widgets
        ui.actionNewCustomWidget->setEnabled(true);

        // Create custom widgets
        QList<QGCToolWidget*> widgets = QGCToolWidget::createWidgetsFromSettings(this);

        if (widgets.size() > 0)
        {
            ui.menuTools->addSeparator();
        }

        for(int i = 0; i < widgets.size(); ++i)
        {
            // Check if this widget already has a parent, do not create it in this case
            QDockWidget* dock = dynamic_cast<QDockWidget*>(widgets.at(i)->parentWidget());
            if (!dock)
            {
                QDockWidget* dock = new QDockWidget(widgets.at(i)->windowTitle(), this);
                dock->setObjectName(widgets.at(i)->objectName()+"_DOCK");
                dock->setWidget(widgets.at(i));
                connect(widgets.at(i), SIGNAL(destroyed()), dock, SLOT(deleteLater()));
                QAction* showAction = new QAction(widgets.at(i)->windowTitle(), this);
                showAction->setCheckable(true);
                connect(showAction, SIGNAL(triggered(bool)), dock, SLOT(setVisible(bool)));
                connect(dock, SIGNAL(visibilityChanged(bool)), showAction, SLOT(setChecked(bool)));
                widgets.at(i)->setMainMenuAction(showAction);
                ui.menuTools->addAction(showAction);
                addDockWidget(Qt::BottomDockWidgetArea, dock);
            }
        }
    }
}

void MainWindow::buildCommonWidgets()
{
    //TODO:  move protocol outside UI
    mavlink     = new MAVLinkProtocol();
    connect(mavlink, SIGNAL(protocolStatusMessage(QString,QString)), this, SLOT(showCriticalMessage(QString,QString)), Qt::QueuedConnection);

    // Dock widgets
    if (!controlDockWidget)
    {
        controlDockWidget = new QDockWidget(tr("Control"), this);
        controlDockWidget->setObjectName("UNMANNED_SYSTEM_CONTROL_DOCKWIDGET");
        controlDockWidget->setWidget( new UASControlWidget(this) );
        addToToolsMenu (controlDockWidget, tr("Control"), SLOT(showToolWidget(bool)), MENU_UAS_CONTROL, Qt::LeftDockWidgetArea);
    }

    if (!listDockWidget)
    {
        listDockWidget = new QDockWidget(tr("Unmanned Systems"), this);
        listDockWidget->setWidget( new UASListWidget(this) );
        listDockWidget->setObjectName("UNMANNED_SYSTEMS_LIST_DOCKWIDGET");
        addToToolsMenu (listDockWidget, tr("Unmanned Systems"), SLOT(showToolWidget(bool)), MENU_UAS_LIST, Qt::RightDockWidgetArea);
    }

    if (!waypointsDockWidget)
    {
        waypointsDockWidget = new QDockWidget(tr("Mission Plan"), this);
        waypointsDockWidget->setWidget( new QGCWaypointListMulti(this) );
        waypointsDockWidget->setObjectName("WAYPOINT_LIST_DOCKWIDGET");
        addToToolsMenu (waypointsDockWidget, tr("Mission Plan"), SLOT(showToolWidget(bool)), MENU_WAYPOINTS, Qt::BottomDockWidgetArea);
    }

    if (!infoDockWidget)
    {
        infoDockWidget = new QDockWidget(tr("Status Details"), this);
        infoDockWidget->setWidget( new UASInfoWidget(this) );
        infoDockWidget->setObjectName("UAS_STATUS_DETAILS_DOCKWIDGET");
        addToToolsMenu (infoDockWidget, tr("Status Details"), SLOT(showToolWidget(bool)), MENU_STATUS, Qt::RightDockWidgetArea);
    }

    if (!debugConsoleDockWidget)
    {
        debugConsoleDockWidget = new QDockWidget(tr("Communication Console"), this);
        debugConsoleDockWidget->setWidget( new DebugConsole(this) );
        debugConsoleDockWidget->setObjectName("COMMUNICATION_DEBUG_CONSOLE_DOCKWIDGET");
        addToToolsMenu (debugConsoleDockWidget, tr("Communication Console"), SLOT(showToolWidget(bool)), MENU_DEBUG_CONSOLE, Qt::BottomDockWidgetArea);
    }

    if (!logPlayerDockWidget)
    {
        logPlayerDockWidget = new QDockWidget(tr("MAVLink Log Player"), this);
        logPlayerDockWidget->setWidget( new QGCMAVLinkLogPlayer(mavlink, this) );
        logPlayerDockWidget->setObjectName("MAVLINK_LOG_PLAYER_DOCKWIDGET");
        addToToolsMenu(logPlayerDockWidget, tr("MAVLink Log Replay"), SLOT(showToolWidget(bool)), MENU_MAVLINK_LOG_PLAYER, Qt::RightDockWidgetArea);
    }

    // Center widgets
    if (!mapWidget)
    {
        mapWidget = new MapWidget(this);
        addToCentralWidgetsMenu (mapWidget, "Maps", SLOT(showCentralWidget()),CENTRAL_MAP);
    }

    if (!protocolWidget)
    {
        protocolWidget    = new XMLCommProtocolWidget(this);
        addToCentralWidgetsMenu (protocolWidget, "Mavlink Generator", SLOT(showCentralWidget()),CENTRAL_PROTOCOL);
    }

#ifdef MAVLINK_ENABLED_SLUGS
    //TODO temporaly debug
    if (!slugsHilSimWidget)
    {
        slugsHilSimWidget = new QDockWidget(tr("Slugs Hil Sim"), this);
        slugsHilSimWidget->setWidget( new SlugsHilSim(this));
        addToToolsMenu (slugsHilSimWidget, tr("HIL Sim Configuration"), SLOT(showToolWidget(bool)), MENU_SLUGS_HIL, Qt::LeftDockWidgetArea);
    }

    //TODO temporaly debug
    if (!slugsCamControlWidget)
    {
        slugsCamControlWidget = new QDockWidget(tr("Slugs Video Camera Control"), this);
        slugsCamControlWidget->setWidget(new SlugsVideoCamControl(this));
        addToToolsMenu (slugsCamControlWidget, tr("Camera Control"), SLOT(showToolWidget(bool)), MENU_SLUGS_CAMERA, Qt::BottomDockWidgetArea);
    }
#endif

    if (!dataplotWidget)
    {
        dataplotWidget    = new QGCDataPlot2D(this);
        addToCentralWidgetsMenu (dataplotWidget, "Logfile Plot", SLOT(showCentralWidget()),CENTRAL_DATA_PLOT);
    }
}


void MainWindow::buildPxWidgets()
{
    //FIXME: memory of acceptList will never be freed again
    QStringList* acceptList = new QStringList();
    acceptList->append("-105,roll deg,deg,+105,s");
    acceptList->append("-105,pitch deg,deg,+105,s");
    acceptList->append("-105,heading deg,deg,+105,s");

    acceptList->append("-60,rollspeed d/s,deg/s,+60,s");
    acceptList->append("-60,pitchspeed d/s,deg/s,+60,s");
    acceptList->append("-60,yawspeed d/s,deg/s,+60,s");
    acceptList->append("0,airspeed,m/s,30");
    acceptList->append("0,groundspeed,m/s,30");
    acceptList->append("0,climbrate,m/s,30");
    acceptList->append("0,throttle,%,100");

    //FIXME: memory of acceptList2 will never be freed again
    QStringList* acceptList2 = new QStringList();
    acceptList2->append("900,servo #1,us,2100,s");
    acceptList2->append("900,servo #2,us,2100,s");
    acceptList2->append("900,servo #3,us,2100,s");
    acceptList2->append("900,servo #4,us,2100,s");
    acceptList2->append("900,servo #5,us,2100,s");
    acceptList2->append("900,servo #6,us,2100,s");
    acceptList2->append("900,servo #7,us,2100,s");
    acceptList2->append("900,servo #8,us,2100,s");
    acceptList2->append("0,abs pressure,hPa,65500");
    //acceptList2->append("-2048,accel. x,raw,2048,s");
    //acceptList2->append("-2048,accel. y,raw,2048,s");
    //acceptList2->append("-2048,accel. z,raw,2048,s");

    if (!linechartWidget)
    {
        // Center widgets
        linechartWidget   = new Linecharts(this);
        addToCentralWidgetsMenu(linechartWidget, tr("Realtime Plot"), SLOT(showCentralWidget()), CENTRAL_LINECHART);
    }


    if (!hudWidget)
    {
        hudWidget         = new HUD(320, 240, this);
        addToCentralWidgetsMenu(hudWidget, tr("Head Up Display"), SLOT(showCentralWidget()), CENTRAL_HUD);
    }

    if (!dataplotWidget)
    {
        dataplotWidget    = new QGCDataPlot2D(this);
        addToCentralWidgetsMenu(dataplotWidget, "Logfile Plot", SLOT(showCentralWidget()), CENTRAL_DATA_PLOT);
    }

#ifdef QGC_OSG_ENABLED
    if (!_3DWidget)
    {
        _3DWidget         = Q3DWidgetFactory::get("PIXHAWK");
        addToCentralWidgetsMenu(_3DWidget, tr("Local 3D"), SLOT(showCentralWidget()), CENTRAL_3D_LOCAL);
    }
#endif

#ifdef QGC_OSGEARTH_ENABLED
    if (!_3DMapWidget)
    {
        _3DMapWidget = Q3DWidgetFactory::get("MAP3D");
        addToCentralWidgetsMenu(_3DMapWidget, tr("OSG Earth 3D"), SLOT(showCentralWidget()), CENTRAL_OSGEARTH);
    }
#endif

#if (defined _MSC_VER) | (defined Q_OS_MAC)
    if (!gEarthWidget)
    {
        gEarthWidget = new QGCGoogleEarthView(this);
        addToCentralWidgetsMenu(gEarthWidget, tr("Google Earth"), SLOT(showCentralWidget()), CENTRAL_GOOGLE_EARTH);
    }

#endif

    // Dock widgets

    if (!detectionDockWidget)
    {
        detectionDockWidget = new QDockWidget(tr("Object Recognition"), this);
        detectionDockWidget->setWidget( new ObjectDetectionView("images/patterns", this) );
        detectionDockWidget->setObjectName("OBJECT_DETECTION_DOCK_WIDGET");
        addToToolsMenu (detectionDockWidget, tr("Object Recognition"), SLOT(showToolWidget(bool)), MENU_DETECTION, Qt::RightDockWidgetArea);
    }

    if (!parametersDockWidget)
    {
        parametersDockWidget = new QDockWidget(tr("Calibration and Onboard Parameters"), this);
        parametersDockWidget->setWidget( new ParameterInterface(this) );
        parametersDockWidget->setObjectName("PARAMETER_INTERFACE_DOCKWIDGET");
        addToToolsMenu (parametersDockWidget, tr("Calibration and Parameters"), SLOT(showToolWidget(bool)), MENU_PARAMETERS, Qt::RightDockWidgetArea);
    }

    if (!watchdogControlDockWidget)
    {
        watchdogControlDockWidget = new QDockWidget(tr("Process Control"), this);
        watchdogControlDockWidget->setWidget( new WatchdogControl(this) );
        watchdogControlDockWidget->setObjectName("WATCHDOG_CONTROL_DOCKWIDGET");
        addToToolsMenu (watchdogControlDockWidget, tr("Process Control"), SLOT(showToolWidget(bool)), MENU_WATCHDOG, Qt::BottomDockWidgetArea);
    }

    if (!hsiDockWidget)
    {
        hsiDockWidget = new QDockWidget(tr("Horizontal Situation Indicator"), this);
        hsiDockWidget->setWidget( new HSIDisplay(this) );
        hsiDockWidget->setObjectName("HORIZONTAL_SITUATION_INDICATOR_DOCK_WIDGET");
        addToToolsMenu (hsiDockWidget, tr("Horizontal Situation"), SLOT(showToolWidget(bool)), MENU_HSI, Qt::BottomDockWidgetArea);
    }

    if (!headDown1DockWidget)
    {
        headDown1DockWidget = new QDockWidget(tr("Flight Display"), this);
        headDown1DockWidget->setWidget( new HDDisplay(acceptList, "Flight Display", this) );
        headDown1DockWidget->setObjectName("HEAD_DOWN_DISPLAY_1_DOCK_WIDGET");
        addToToolsMenu (headDown1DockWidget, tr("Flight Display"), SLOT(showToolWidget(bool)), MENU_HDD_1, Qt::RightDockWidgetArea);
    }

    if (!headDown2DockWidget)
    {
        headDown2DockWidget = new QDockWidget(tr("Actuator Status"), this);
        headDown2DockWidget->setWidget( new HDDisplay(acceptList2, "Actuator Status", this) );
        headDown2DockWidget->setObjectName("HEAD_DOWN_DISPLAY_2_DOCK_WIDGET");
        addToToolsMenu (headDown2DockWidget, tr("Actuator Status"), SLOT(showToolWidget(bool)), MENU_HDD_2, Qt::RightDockWidgetArea);
    }

    if (!rcViewDockWidget)
    {
        rcViewDockWidget = new QDockWidget(tr("Radio Control"), this);
        rcViewDockWidget->setWidget( new QGCRemoteControlView(this) );
        rcViewDockWidget->setObjectName("RADIO_CONTROL_CHANNELS_DOCK_WIDGET");
        addToToolsMenu (rcViewDockWidget, tr("Radio Control"), SLOT(showToolWidget(bool)), MENU_RC_VIEW, Qt::BottomDockWidgetArea);
    }

    if (!headUpDockWidget)
    {
        headUpDockWidget = new QDockWidget(tr("HUD"), this);
        headUpDockWidget->setWidget( new HUD(320, 240, this));
        headUpDockWidget->setObjectName("HEAD_UP_DISPLAY_DOCK_WIDGET");
        addToToolsMenu (headUpDockWidget, tr("Head Up Display"), SLOT(showToolWidget(bool)), MENU_HUD, Qt::RightDockWidgetArea);
    }

    if (!video1DockWidget)
    {
        video1DockWidget = new QDockWidget(tr("Video Stream 1"), this);
        HUD* video1 =  new HUD(160, 120, this);
        video1->enableHUDInstruments(false);
        video1->enableVideo(true);
        // FIXME select video stream as well
        video1DockWidget->setWidget(video1);
        video1DockWidget->setObjectName("VIDEO_STREAM_1_DOCK_WIDGET");
        addToToolsMenu (video1DockWidget, tr("Video Stream 1"), SLOT(showToolWidget(bool)), MENU_VIDEO_STREAM_1, Qt::LeftDockWidgetArea);
    }

    if (!video2DockWidget)
    {
        video2DockWidget = new QDockWidget(tr("Video Stream 2"), this);
        HUD* video2 =  new HUD(160, 120, this);
        video2->enableHUDInstruments(false);
        video2->enableVideo(true);
        // FIXME select video stream as well
        video2DockWidget->setWidget(video2);
        video2DockWidget->setObjectName("VIDEO_STREAM_2_DOCK_WIDGET");
        addToToolsMenu (video2DockWidget, tr("Video Stream 2"), SLOT(showToolWidget(bool)), MENU_VIDEO_STREAM_2, Qt::LeftDockWidgetArea);
    }

    // Dialogue widgets
    //FIXME: free memory in destructor
}

void MainWindow::buildSlugsWidgets()
{
    if (!linechartWidget)
    {
        // Center widgets
        linechartWidget   = new Linecharts(this);
        addToCentralWidgetsMenu(linechartWidget, tr("Realtime Plot"), SLOT(showCentralWidget()), CENTRAL_LINECHART);
    }

    if (!headUpDockWidget)
    {
        // Dock widgets
        headUpDockWidget = new QDockWidget(tr("Control Indicator"), this);
        headUpDockWidget->setWidget( new HUD(320, 240, this));
        headUpDockWidget->setObjectName("HEAD_UP_DISPLAY_DOCK_WIDGET");
        addToToolsMenu (headUpDockWidget, tr("Head Up Display"), SLOT(showToolWidget(bool)), MENU_HUD, Qt::LeftDockWidgetArea);
    }

    if (!rcViewDockWidget)
    {
        rcViewDockWidget = new QDockWidget(tr("Radio Control"), this);
        rcViewDockWidget->setWidget( new QGCRemoteControlView(this) );
        rcViewDockWidget->setObjectName("RADIO_CONTROL_CHANNELS_DOCK_WIDGET");
        addToToolsMenu (rcViewDockWidget, tr("Radio Control"), SLOT(showToolWidget(bool)), MENU_RC_VIEW, Qt::BottomDockWidgetArea);
    }

    if (!slugsDataWidget)
    {
        // Dialog widgets
        slugsDataWidget = new QDockWidget(tr("Slugs Data"), this);
        slugsDataWidget->setWidget( new SlugsDataSensorView(this));
        slugsDataWidget->setObjectName("SLUGS_DATA_DOCK_WIDGET");
        addToToolsMenu (slugsDataWidget, tr("Telemetry Data"), SLOT(showToolWidget(bool)), MENU_SLUGS_DATA, Qt::RightDockWidgetArea);
    }

    if (!slugsPIDControlWidget)
    {
        slugsPIDControlWidget = new QDockWidget(tr("Slugs PID Control"), this);
        slugsPIDControlWidget->setWidget(new SlugsPIDControl(this));
        slugsPIDControlWidget->setObjectName("SLUGS_PID_CONTROL_DOCK_WIDGET");
        addToToolsMenu (slugsPIDControlWidget, tr("PID Configuration"), SLOT(showToolWidget(bool)), MENU_SLUGS_PID, Qt::LeftDockWidgetArea);
    }

    if (!slugsHilSimWidget)
    {
        slugsHilSimWidget = new QDockWidget(tr("Slugs Hil Sim"), this);
        slugsHilSimWidget->setWidget( new SlugsHilSim(this));
        slugsHilSimWidget->setObjectName("SLUGS_HIL_SIM_DOCK_WIDGET");
        addToToolsMenu (slugsHilSimWidget, tr("HIL Sim Configuration"), SLOT(showToolWidget(bool)), MENU_SLUGS_HIL, Qt::LeftDockWidgetArea);
    }

    if (!slugsCamControlWidget)
    {
        slugsCamControlWidget = new QDockWidget(tr("Slugs Video Camera Control"), this);
        slugsCamControlWidget->setWidget(new SlugsVideoCamControl(this));
        slugsCamControlWidget->setObjectName("SLUGS_CAM_CONTROL_DOCK_WIDGET");
        addToToolsMenu (slugsCamControlWidget, tr("Camera Control"), SLOT(showToolWidget(bool)), MENU_SLUGS_CAMERA, Qt::BottomDockWidgetArea);
    }
}


void MainWindow::addToCentralWidgetsMenu ( QWidget* widget,
                                           const QString title,
                                           const char * slotName,
                                           TOOLS_WIDGET_NAMES centralWidget)
{
    QAction* tempAction;


// Not needed any more - separate menu now available

//    // Add the separator that will separate tools from central Widgets
//    if (!toolsMenuActions[CENTRAL_SEPARATOR])
//    {
//        tempAction = ui.menuTools->addSeparator();
//        toolsMenuActions[CENTRAL_SEPARATOR] = tempAction;
//        tempAction->setData(CENTRAL_SEPARATOR);
//    }

    tempAction = ui.menuMain->addAction(title);

    tempAction->setCheckable(true);
    tempAction->setData(centralWidget);

    // populate the Hashes
    toolsMenuActions[centralWidget] = tempAction;
    dockWidgets[centralWidget] = widget;

    QString chKey = buildMenuKey(SUB_SECTION_CHECKED, centralWidget, currentView);

    if (!settings.contains(chKey))
    {
        settings.setValue(chKey,false);
        tempAction->setChecked(false);
    }
    else
    {
        tempAction->setChecked(settings.value(chKey).toBool());
    }

    // connect the action
    connect(tempAction,SIGNAL(triggered(bool)),this, slotName);
}


void MainWindow::showCentralWidget()
{
    QAction* senderAction = qobject_cast<QAction *>(sender());

    // Block sender action while manipulating state
    senderAction->blockSignals(true);

    int tool = senderAction->data().toInt();
    QString chKey;

    // check the current action

    if (senderAction && dockWidgets[tool])
    {
        // uncheck all central widget actions
        QHashIterator<int, QAction*> i(toolsMenuActions);
        while (i.hasNext())
        {
            i.next();
            //qDebug() << "shCW" << i.key() << "read";
            if (i.value() && i.value()->data().toInt() > 255)
            {
                // Block signals and uncheck action
                // firing would be unneccesary
                i.value()->blockSignals(true);
                i.value()->setChecked(false);
                i.value()->blockSignals(false);

                // update the settings
                chKey = buildMenuKey (SUB_SECTION_CHECKED,static_cast<TOOLS_WIDGET_NAMES>(i.value()->data().toInt()), currentView);
                settings.setValue(chKey,false);
            }
        }

        // check the current action
        //qDebug() << senderAction->text();
        senderAction->setChecked(true);

        // update the central widget
        centerStack->setCurrentWidget(dockWidgets[tool]);

        // store the selected central widget
        chKey = buildMenuKey (SUB_SECTION_CHECKED,static_cast<TOOLS_WIDGET_NAMES>(tool), currentView);
        settings.setValue(chKey,true);

        // Unblock sender action
        senderAction->blockSignals(false);

        presentView();
    }
}

/**
 * Adds a widget to the tools menu and sets it visible if it was
 * enabled last time.
 */
void MainWindow::addToToolsMenu ( QWidget* widget,
                                  const QString title,
                                  const char * slotName,
                                  TOOLS_WIDGET_NAMES tool,
                                  Qt::DockWidgetArea location)
{
    QAction* tempAction;
    QString posKey, chKey;


    if (toolsMenuActions[CENTRAL_SEPARATOR])
    {
        tempAction = new QAction(title, this);
        ui.menuTools->insertAction(toolsMenuActions[CENTRAL_SEPARATOR],
                                   tempAction);
    }
    else
    {
        tempAction = ui.menuTools->addAction(title);
    }

    tempAction->setCheckable(true);
    tempAction->setData(tool);

    // populate the Hashes
    toolsMenuActions[tool] = tempAction;
    dockWidgets[tool] = widget;
    //qDebug() << widget;

    posKey = buildMenuKey (SUB_SECTION_LOCATION,tool, currentView);

    if (!settings.contains(posKey))
    {
        settings.setValue(posKey,location);
        dockWidgetLocations[tool] = location;
    }
    else
    {
        dockWidgetLocations[tool] = static_cast <Qt::DockWidgetArea> (settings.value(posKey, Qt::RightDockWidgetArea).toInt());
    }

    chKey = buildMenuKey(SUB_SECTION_CHECKED,tool, currentView);

    if (!settings.contains(chKey))
    {
        settings.setValue(chKey,false);
        tempAction->setChecked(false);
        widget->setVisible(false);
    }
    else
    {
        tempAction->setChecked(settings.value(chKey, false).toBool());
        widget->setVisible(settings.value(chKey, false).toBool());
    }

    // connect the action
    connect(tempAction,SIGNAL(toggled(bool)),this, slotName);

    connect(qobject_cast <QDockWidget *>(dockWidgets[tool]),
            SIGNAL(visibilityChanged(bool)), this, SLOT(showToolWidget(bool)));

    //  connect(qobject_cast <QDockWidget *>(dockWidgets[tool]),
    //          SIGNAL(visibilityChanged(bool)), this, SLOT(updateVisibilitySettings(bool)));

    connect(qobject_cast <QDockWidget *>(dockWidgets[tool]),
            SIGNAL(dockLocationChanged(Qt::DockWidgetArea)), this, SLOT(updateLocationSettings(Qt::DockWidgetArea)));
}

void MainWindow::showToolWidget(bool visible)
{
    if (!aboutToCloseFlag && !changingViewsFlag)
    {
        QAction* action = qobject_cast<QAction *>(sender());

        // Prevent this to fire if undocked
        if (action)
        {
            int tool = action->data().toInt();

            QDockWidget* dockWidget = qobject_cast<QDockWidget *> (dockWidgets[tool]);

            if (dockWidget && dockWidget->isVisible() != visible)
            {
                if (visible)
                {
                    addDockWidget(dockWidgetLocations[tool], dockWidget);
                    dockWidget->show();
                }
                else
                {
                    removeDockWidget(dockWidget);
                }

                QHashIterator<int, QWidget*> i(dockWidgets);
                while (i.hasNext())
                {
                    i.next();
                    if ((static_cast <QDockWidget *>(dockWidgets[i.key()])) == dockWidget)
                    {
                        QString chKey = buildMenuKey (SUB_SECTION_CHECKED,static_cast<TOOLS_WIDGET_NAMES>(i.key()), currentView);
                        settings.setValue(chKey,visible);
                        //qDebug() << "showToolWidget(): Set key" << chKey << "to" << visible;
                        break;
                    }
                }
            }
        }

        QDockWidget* dockWidget = qobject_cast<QDockWidget*>(QObject::sender());

        //qDebug() << "Trying to cast dockwidget" << dockWidget << "isvisible" << visible;

        if (dockWidget)
        {
            // Get action
            int tool = dockWidgets.key(dockWidget);

            //qDebug() << "Updating widget setting" << tool << "to" << visible;

            QAction* action = toolsMenuActions[tool];
            action->blockSignals(true);
            action->setChecked(visible);
            action->blockSignals(false);

            QHashIterator<int, QWidget*> i(dockWidgets);
            while (i.hasNext())
            {
                i.next();
                if ((static_cast <QDockWidget *>(dockWidgets[i.key()])) == dockWidget)
                {
                    QString chKey = buildMenuKey (SUB_SECTION_CHECKED,static_cast<TOOLS_WIDGET_NAMES>(i.key()), currentView);
                    settings.setValue(chKey,visible);
                   // qDebug() << "showToolWidget(): Set key" << chKey << "to" << visible;
                    break;
                }
            }
        }
    }
}


void MainWindow::showTheWidget (TOOLS_WIDGET_NAMES widget, VIEW_SECTIONS view)
{
    bool tempVisible;
    Qt::DockWidgetArea tempLocation;
    QDockWidget* tempWidget = static_cast <QDockWidget *>(dockWidgets[widget]);

    tempVisible =  settings.value(buildMenuKey(SUB_SECTION_CHECKED,widget,view), false).toBool();

     //qDebug() << "showTheWidget(): Set key" << buildMenuKey(SUB_SECTION_CHECKED,widget,view) << "to" << tempVisible;

    if (tempWidget)
    {
        toolsMenuActions[widget]->setChecked(tempVisible);
    }


    //qDebug() <<  buildMenuKey (SUB_SECTION_CHECKED,widget,view) << tempVisible;

    tempLocation = static_cast <Qt::DockWidgetArea>(settings.value(buildMenuKey (SUB_SECTION_LOCATION,widget, view), QVariant(Qt::RightDockWidgetArea)).toInt());

    if (tempWidget != NULL)
    {
        if (tempVisible)
        {
            addDockWidget(tempLocation, tempWidget);
            tempWidget->show();
        }
    }
}

QString MainWindow::buildMenuKey(SETTINGS_SECTIONS section, TOOLS_WIDGET_NAMES tool, VIEW_SECTIONS view)
{
    // Key is built as follows: autopilot_type/section_menu/view/tool/section
    int apType;

//    apType = (UASManager::instance() && UASManager::instance()->silentGetActiveUAS())?
//             UASManager::instance()->getActiveUAS()->getAutopilotType():
//             -1;

    apType = 1;

    return (QString::number(apType) + "_" +
            QString::number(SECTION_MENU) + "_" +
            QString::number(view) + "_" +
            QString::number(tool) + "_" +
            QString::number(section) + "_" );
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    storeSettings();
    aboutToCloseFlag = true;
    mavlink->storeSettings();
    UASManager::instance()->storeSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::showDockWidget (bool vis)
{
    if (!aboutToCloseFlag && !changingViewsFlag)
    {
        QDockWidget* temp = qobject_cast<QDockWidget *>(sender());

        if (temp)
        {
            QHashIterator<int, QWidget*> i(dockWidgets);
            while (i.hasNext())
            {
                i.next();
                if ((static_cast <QDockWidget *>(dockWidgets[i.key()])) == temp)
                {
                    QString chKey = buildMenuKey (SUB_SECTION_CHECKED,static_cast<TOOLS_WIDGET_NAMES>(i.key()), currentView);
                    settings.setValue(chKey,vis);
                    toolsMenuActions[i.key()]->setChecked(vis);
                    break;
                }
            }
        }
    }
}

void MainWindow::updateVisibilitySettings (bool vis)
{
    if (!aboutToCloseFlag && !changingViewsFlag)
    {
        QDockWidget* temp = qobject_cast<QDockWidget *>(sender());

        if (temp)
        {
            QHashIterator<int, QWidget*> i(dockWidgets);
            while (i.hasNext())
            {
                i.next();
                if ((static_cast <QDockWidget *>(dockWidgets[i.key()])) == temp)
                {
                    QString chKey = buildMenuKey (SUB_SECTION_CHECKED,static_cast<TOOLS_WIDGET_NAMES>(i.key()), currentView);
                    settings.setValue(chKey,vis);
                    toolsMenuActions[i.key()]->setChecked(vis);
                    break;
                }
            }
        }
    }
}

void MainWindow::updateLocationSettings (Qt::DockWidgetArea location)
{
    QDockWidget* temp = qobject_cast<QDockWidget *>(sender());

    QHashIterator<int, QWidget*> i(dockWidgets);
    while (i.hasNext())
    {
        i.next();
        if ((static_cast <QDockWidget *>(dockWidgets[i.key()])) == temp)
        {
            QString posKey = buildMenuKey (SUB_SECTION_LOCATION,static_cast<TOOLS_WIDGET_NAMES>(i.key()), currentView);
            settings.setValue(posKey,location);
            break;
        }
    }
}

/**
 * Connect the signals and slots of the common window widgets
 */
void MainWindow::connectCommonWidgets()
{
    if (infoDockWidget && infoDockWidget->widget())
    {
        connect(mavlink, SIGNAL(receiveLossChanged(int, float)),
                infoDockWidget->widget(), SLOT(updateSendLoss(int, float)));
    }

    if (mapWidget && waypointsDockWidget->widget())
    {

    }

    //TODO temporaly debug
    if (slugsHilSimWidget && slugsHilSimWidget->widget()){
        connect(UASManager::instance(), SIGNAL(activeUASSet(UASInterface*)),
                slugsHilSimWidget->widget(), SLOT(activeUasSet(UASInterface*)));
    }
}

void MainWindow::createCustomWidget()
{
    QGCToolWidget* tool = new QGCToolWidget("Unnamed Tool", this);

    if (QGCToolWidget::instances()->size() < 2)
    {
        // This is the first widget
        ui.menuTools->addSeparator();
    }

    QDockWidget* dock = new QDockWidget("Unnamed Tool", this);
    connect(tool, SIGNAL(destroyed()), dock, SLOT(deleteLater()));
    dock->setWidget(tool);
    QAction* showAction = new QAction("Show Unnamed Tool", this);
    showAction->setCheckable(true);
    connect(dock, SIGNAL(visibilityChanged(bool)), showAction, SLOT(setChecked(bool)));
    connect(showAction, SIGNAL(triggered(bool)), dock, SLOT(setVisible(bool)));
    tool->setMainMenuAction(showAction);
    ui.menuTools->addAction(showAction);
    this->addDockWidget(Qt::BottomDockWidgetArea, dock);
    dock->setVisible(true);
}

void MainWindow::connectPxWidgets()
{
    // No special connections necessary at this point
}

void MainWindow::connectSlugsWidgets()
{
    if (slugsHilSimWidget && slugsHilSimWidget->widget()){
        connect(UASManager::instance(), SIGNAL(activeUASSet(UASInterface*)),
                slugsHilSimWidget->widget(), SLOT(activeUasSet(UASInterface*)));
    }

    if (slugsDataWidget && slugsDataWidget->widget()){
        connect(UASManager::instance(), SIGNAL(activeUASSet(UASInterface*)),
                slugsDataWidget->widget(), SLOT(setActiveUAS(UASInterface*)));
    }


}

void MainWindow::arrangeCommonCenterStack()
{
    centerStack = new QStackedWidget(this);
    centerStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    if (!centerStack) return;

    if (mapWidget && (centerStack->indexOf(mapWidget) == -1)) centerStack->addWidget(mapWidget);
    if (dataplotWidget && (centerStack->indexOf(dataplotWidget) == -1)) centerStack->addWidget(dataplotWidget);
    if (protocolWidget && (centerStack->indexOf(protocolWidget) == -1)) centerStack->addWidget(protocolWidget);

    setCentralWidget(centerStack);
}

void MainWindow::arrangePxCenterStack()
{

    if (!centerStack) {
        qDebug() << "Center Stack not Created!";
        return;
    }


    if (linechartWidget && (centerStack->indexOf(linechartWidget) == -1)) centerStack->addWidget(linechartWidget);

#ifdef QGC_OSG_ENABLED
    if (_3DWidget && (centerStack->indexOf(_3DWidget) == -1)) centerStack->addWidget(_3DWidget);
#endif
#ifdef QGC_OSGEARTH_ENABLED
    if (_3DMapWidget && (centerStack->indexOf(_3DMapWidget) == -1)) centerStack->addWidget(_3DMapWidget);
#endif
#if (defined _MSC_VER) | (defined Q_OS_MAC)
    if (gEarthWidget && (centerStack->indexOf(gEarthWidget) == -1)) centerStack->addWidget(gEarthWidget);
#endif
    if (hudWidget && (centerStack->indexOf(hudWidget) == -1)) centerStack->addWidget(hudWidget);
    if (dataplotWidget && (centerStack->indexOf(dataplotWidget) == -1)) centerStack->addWidget(dataplotWidget);
}

void MainWindow::arrangeSlugsCenterStack()
{

    if (!centerStack) {
        qDebug() << "Center Stack not Created!";
        return;
    }

    if (linechartWidget && (centerStack->indexOf(linechartWidget) == -1)) centerStack->addWidget(linechartWidget);
    if (hudWidget && (centerStack->indexOf(hudWidget) == -1)) centerStack->addWidget(hudWidget);

}

void MainWindow::loadSettings()
{
    QSettings settings;
    settings.beginGroup("QGC_MAINWINDOW");
    autoReconnect = settings.value("AUTO_RECONNECT", autoReconnect).toBool();
    currentStyle = (QGC_MAINWINDOW_STYLE)settings.value("CURRENT_STYLE", currentStyle).toInt();
    settings.endGroup();
}

void MainWindow::storeSettings()
{
    QSettings settings;
    settings.beginGroup("QGC_MAINWINDOW");
    settings.setValue("AUTO_RECONNECT", autoReconnect);
    settings.setValue("CURRENT_STYLE", currentStyle);
    settings.endGroup();
    settings.setValue(getWindowGeometryKey(), saveGeometry());
    // Save the last current view in any case
    settings.setValue("CURRENT_VIEW", currentView);
    // Save the current window state, but only if a system is connected (else no real number of widgets would be present)
    if (UASManager::instance()->getUASList().length() > 0) settings.setValue(getWindowStateKey(), saveState(QGC::applicationVersion()));
    // Save the current view only if a UAS is connected
    if (UASManager::instance()->getUASList().length() > 0) settings.setValue("CURRENT_VIEW_WITH_UAS_CONNECTED", currentView);
    settings.sync();
}

void MainWindow::configureWindowName()
{
    QList<QHostAddress> hostAddresses = QNetworkInterface::allAddresses();
    QString windowname = qApp->applicationName() + " " + qApp->applicationVersion();
    bool prevAddr = false;

    windowname.append(" (" + QHostInfo::localHostName() + ": ");

    for (int i = 0; i < hostAddresses.size(); i++)
    {
        // Exclude loopback IPv4 and all IPv6 addresses
        if (hostAddresses.at(i) != QHostAddress("127.0.0.1") && !hostAddresses.at(i).toString().contains(":"))
        {
            if(prevAddr) windowname.append("/");
            windowname.append(hostAddresses.at(i).toString());
            prevAddr = true;
        }
    }

    windowname.append(")");

    setWindowTitle(windowname);

#ifndef Q_WS_MAC
    //qApp->setWindowIcon(QIcon(":/core/images/qtcreator_logo_128.png"));
#endif
}

void MainWindow::startVideoCapture()
{
    QString format = "bmp";
    QString initialPath = QDir::currentPath() + tr("/untitled.") + format;

    QString screenFileName = QFileDialog::getSaveFileName(this, tr("Save As"),
                                                          initialPath,
                                                          tr("%1 Files (*.%2);;All Files (*)")
                                                          .arg(format.toUpper())
                                                          .arg(format));
    delete videoTimer;
    videoTimer = new QTimer(this);
    //videoTimer->setInterval(40);
    //connect(videoTimer, SIGNAL(timeout()), this, SLOT(saveScreen()));
    //videoTimer->stop();
}

void MainWindow::stopVideoCapture()
{
    videoTimer->stop();

    // TODO Convert raw images to PNG
}

void MainWindow::saveScreen()
{
    QPixmap window = QPixmap::grabWindow(this->winId());
    QString format = "bmp";

    if (!screenFileName.isEmpty())
    {
        window.save(screenFileName, format.toAscii());
    }
}

void MainWindow::enableAutoReconnect(bool enabled)
{
    autoReconnect = enabled;
}

void MainWindow::loadNativeStyle()
{
    loadStyle(QGC_MAINWINDOW_STYLE_NATIVE);
}

void MainWindow::loadIndoorStyle()
{
    loadStyle(QGC_MAINWINDOW_STYLE_INDOOR);
}

void MainWindow::loadOutdoorStyle()
{
    loadStyle(QGC_MAINWINDOW_STYLE_OUTDOOR);
}

void MainWindow::loadStyle(QGC_MAINWINDOW_STYLE style)
{
    switch (style)
    {
    case QGC_MAINWINDOW_STYLE_NATIVE:
        {
            // Native mode means setting no style
            // so if we were already in native mode
            // take no action
            // Only if a style was set, remove it.
            if (style != currentStyle)
            {
                qApp->setStyleSheet("");
                showInfoMessage(tr("Please restart QGroundControl"), tr("Please restart QGroundControl to switch to fully native look and feel. Currently you have loaded Qt's plastique style."));
            }
        }
        break;
    case QGC_MAINWINDOW_STYLE_INDOOR:
        qApp->setStyle("plastique");
        styleFileName = ":/images/style-mission.css";
        reloadStylesheet();
        break;
    case QGC_MAINWINDOW_STYLE_OUTDOOR:
        qApp->setStyle("plastique");
        styleFileName = ":/images/style-outdoor.css";
        reloadStylesheet();
        break;
    }
    currentStyle = style;
}

void MainWindow::selectStylesheet()
{
    // Let user select style sheet
    styleFileName = QFileDialog::getOpenFileName(this, tr("Specify stylesheet"), styleFileName, tr("CSS Stylesheet (*.css);;"));

    if (!styleFileName.endsWith(".css"))
    {
        QMessageBox msgBox;
        msgBox.setIcon(QMessageBox::Information);
        msgBox.setText(tr("QGroundControl did lot load a new style"));
        msgBox.setInformativeText(tr("No suitable .css file selected. Please select a valid .css file."));
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);
        msgBox.exec();
        return;
    }

    // Load style sheet
    reloadStylesheet();
}

void MainWindow::reloadStylesheet()
{
    // Load style sheet
    QFile* styleSheet = new QFile(styleFileName);
    if (!styleSheet->exists())
    {
        styleSheet = new QFile(":/images/style-mission.css");
    }
    if (styleSheet->open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QString style = QString(styleSheet->readAll());
        style.replace("ICONDIR", QCoreApplication::applicationDirPath()+ "/images/");
        qApp->setStyleSheet(style);
    }
    else
    {
        QMessageBox msgBox;
        msgBox.setIcon(QMessageBox::Information);
        msgBox.setText(tr("QGroundControl did lot load a new style"));
        msgBox.setInformativeText(tr("Stylesheet file %1 was not readable").arg(styleFileName));
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);
        msgBox.exec();
    }
    delete styleSheet;
}

/**
 * The status message will be overwritten if a new message is posted to this function
 *
 * @param status message text
 * @param timeout how long the status should be displayed
 */
void MainWindow::showStatusMessage(const QString& status, int timeout)
{
    statusBar()->showMessage(status, timeout);
}

/**
 * The status message will be overwritten if a new message is posted to this function.
 * it will be automatically hidden after 5 seconds.
 *
 * @param status message text
 */
void MainWindow::showStatusMessage(const QString& status)
{
    statusBar()->showMessage(status, 20000);
}

void MainWindow::showCriticalMessage(const QString& title, const QString& message)
{
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Critical);
    msgBox.setText(title);
    msgBox.setInformativeText(message);
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setDefaultButton(QMessageBox::Ok);
    msgBox.exec();
}

void MainWindow::showInfoMessage(const QString& title, const QString& message)
{
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setText(title);
    msgBox.setInformativeText(message);
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setDefaultButton(QMessageBox::Ok);
    msgBox.exec();
}

/**
* @brief Create all actions associated to the main window
*
**/
void MainWindow::connectCommonActions()
{
    ui.actionNewCustomWidget->setEnabled(false);

    // Bind together the perspective actions
    QActionGroup* perspectives = new QActionGroup(ui.menuPerspectives);
    perspectives->addAction(ui.actionEngineersView);
    perspectives->addAction(ui.actionMavlinkView);
    perspectives->addAction(ui.actionPilotsView);
    perspectives->addAction(ui.actionOperatorsView);
    perspectives->addAction(ui.actionUnconnectedView);
    perspectives->setExclusive(true);

    // Mark the right one as selected
    if (currentView == VIEW_ENGINEER) ui.actionEngineersView->setChecked(true);
    if (currentView == VIEW_MAVLINK) ui.actionMavlinkView->setChecked(true);
    if (currentView == VIEW_PILOT) ui.actionPilotsView->setChecked(true);
    if (currentView == VIEW_OPERATOR) ui.actionOperatorsView->setChecked(true);
    if (currentView == VIEW_UNCONNECTED) ui.actionUnconnectedView->setChecked(true);

    // The pilot, engineer and operator view are not available on startup
    // since they only make sense with a system connected.
    ui.actionPilotsView->setEnabled(false);
    ui.actionOperatorsView->setEnabled(false);
    ui.actionEngineersView->setEnabled(false);
    // The UAS actions are not enabled without connection to system
    ui.actionLiftoff->setEnabled(false);
    ui.actionLand->setEnabled(false);
    ui.actionEmergency_Kill->setEnabled(false);
    ui.actionEmergency_Land->setEnabled(false);
    ui.actionShutdownMAV->setEnabled(false);

    // Connect actions from ui
    connect(ui.actionAdd_Link, SIGNAL(triggered()), this, SLOT(addLink()));

    // Connect internal actions
    connect(UASManager::instance(), SIGNAL(UASCreated(UASInterface*)), this, SLOT(UASCreated(UASInterface*)));
    connect(UASManager::instance(), SIGNAL(activeUASSet(UASInterface*)), this, SLOT(setActiveUAS(UASInterface*)));

    // Unmanned System controls
    connect(ui.actionLiftoff, SIGNAL(triggered()), UASManager::instance(), SLOT(launchActiveUAS()));
    connect(ui.actionLand, SIGNAL(triggered()), UASManager::instance(), SLOT(returnActiveUAS()));
    connect(ui.actionEmergency_Land, SIGNAL(triggered()), UASManager::instance(), SLOT(stopActiveUAS()));
    connect(ui.actionEmergency_Kill, SIGNAL(triggered()), UASManager::instance(), SLOT(killActiveUAS()));
    connect(ui.actionShutdownMAV, SIGNAL(triggered()), UASManager::instance(), SLOT(shutdownActiveUAS()));
    connect(ui.actionConfiguration, SIGNAL(triggered()), UASManager::instance(), SLOT(configureActiveUAS()));

    // Views actions
    connect(ui.actionPilotsView, SIGNAL(triggered()), this, SLOT(loadPilotView()));
    connect(ui.actionEngineersView, SIGNAL(triggered()), this, SLOT(loadEngineerView()));
    connect(ui.actionOperatorsView, SIGNAL(triggered()), this, SLOT(loadOperatorView()));
    connect(ui.actionUnconnectedView, SIGNAL(triggered()), this, SLOT(loadUnconnectedView()));

    connect(ui.actionMavlinkView, SIGNAL(triggered()), this, SLOT(loadMAVLinkView()));
    connect(ui.actionReloadStylesheet, SIGNAL(triggered()), this, SLOT(reloadStylesheet()));
    connect(ui.actionSelectStylesheet, SIGNAL(triggered()), this, SLOT(selectStylesheet()));

    // Help Actions
    connect(ui.actionOnline_Documentation, SIGNAL(triggered()), this, SLOT(showHelp()));
    connect(ui.actionDeveloper_Credits, SIGNAL(triggered()), this, SLOT(showCredits()));
    connect(ui.actionProject_Roadmap_2, SIGNAL(triggered()), this, SLOT(showRoadMap()));

    // Custom widget actions
    connect(ui.actionNewCustomWidget, SIGNAL(triggered()), this, SLOT(createCustomWidget()));

    // Audio output
    ui.actionMuteAudioOutput->setChecked(GAudioOutput::instance()->isMuted());
    connect(GAudioOutput::instance(), SIGNAL(mutedChanged(bool)), ui.actionMuteAudioOutput, SLOT(setChecked(bool)));
    connect(ui.actionMuteAudioOutput, SIGNAL(triggered(bool)), GAudioOutput::instance(), SLOT(mute(bool)));

    // User interaction
    // NOTE: Joystick thread is not started and
    // configuration widget is not instantiated
    // unless it is actually used
    // so no ressources spend on this.
    ui.actionJoystickSettings->setVisible(true);

    // Configuration
    // Joystick
    connect(ui.actionJoystickSettings, SIGNAL(triggered()), this, SLOT(configure()));
    // Application Settings
    connect(ui.actionSettings, SIGNAL(triggered()), this, SLOT(showSettings()));
}

void MainWindow::connectPxActions()
{


}

void MainWindow::connectSlugsActions()
{

}

void MainWindow::showHelp()
{
    if(!QDesktopServices::openUrl(QUrl("http://qgroundcontrol.org/users/")))
    {
        QMessageBox msgBox;
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.setText("Could not open help in browser");
        msgBox.setInformativeText("To get to the online help, please open http://qgroundcontrol.org/user_guide in a browser.");
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);
        msgBox.exec();
    }
}

void MainWindow::showCredits()
{
    if(!QDesktopServices::openUrl(QUrl("http://qgroundcontrol.org/credits/")))
    {
        QMessageBox msgBox;
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.setText("Could not open credits in browser");
        msgBox.setInformativeText("To get to the online help, please open http://qgroundcontrol.org/credits in a browser.");
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);
        msgBox.exec();
    }
}

void MainWindow::showRoadMap()
{
    if(!QDesktopServices::openUrl(QUrl("http://qgroundcontrol.org/roadmap/")))
    {
        QMessageBox msgBox;
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.setText("Could not open roadmap in browser");
        msgBox.setInformativeText("To get to the online help, please open http://qgroundcontrol.org/roadmap in a browser.");
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);
        msgBox.exec();
    }
}

void MainWindow::configure()
{
    if (!joystickWidget)
    {
        if (!joystick->isRunning())
        {
            joystick->start();
        }
        joystickWidget = new JoystickWidget(joystick);
    }
    joystickWidget->show();
}

void MainWindow::showSettings()
{
    QGCSettingsWidget* settings = new QGCSettingsWidget(this);
    settings->show();
}

void MainWindow::addLink()
{
    SerialLink* link = new SerialLink();
    // TODO This should be only done in the dialog itself

    LinkManager::instance()->add(link);
    LinkManager::instance()->addProtocol(link, mavlink);

    // Go fishing for this link's configuration window
    QList<QAction*> actions = ui.menuNetwork->actions();

    foreach (QAction* act, actions)
    {
        if (act->data().toInt() == LinkManager::instance()->getLinks().indexOf(link))
        {
            act->trigger();
            break;
        }
    }
}

void MainWindow::addLink(LinkInterface *link)
{
    // IMPORTANT! KEEP THESE TWO LINES
    // THEY MAKE SURE THE LINK IS PROPERLY REGISTERED
    // BEFORE LINKING THE UI AGAINST IT
    // Register (does nothing if already registered)
    LinkManager::instance()->add(link);
    LinkManager::instance()->addProtocol(link, mavlink);

    // Go fishing for this link's configuration window
    QList<QAction*> actions = ui.menuNetwork->actions();

    bool found = false;

    foreach (QAction* act, actions)
    {
        if (act->data().toInt() == LinkManager::instance()->getLinks().indexOf(link))
        {
            found = true;
        }
    }

    UDPLink* udp = dynamic_cast<UDPLink*>(link);

    if (!found || udp)
    {
        CommConfigurationWindow* commWidget = new CommConfigurationWindow(link, mavlink, this);
        QAction* action = commWidget->getAction();
        ui.menuNetwork->addAction(action);

        // Error handling
        connect(link, SIGNAL(communicationError(QString,QString)), this, SLOT(showCriticalMessage(QString,QString)), Qt::QueuedConnection);
        // Special case for simulationlink
        MAVLinkSimulationLink* sim = dynamic_cast<MAVLinkSimulationLink*>(link);
        if (sim)
        {
            //connect(sim, SIGNAL(valueChanged(int,QString,double,quint64)), linechart, SLOT(appendData(int,QString,double,quint64)));
            connect(ui.actionSimulate, SIGNAL(triggered(bool)), sim, SLOT(connectLink(bool)));
        }
    }
}

void MainWindow::setActiveUAS(UASInterface* uas)
{
    // Enable and rename menu
    ui.menuUnmanned_System->setTitle(uas->getUASName());
    if (!ui.menuUnmanned_System->isEnabled()) ui.menuUnmanned_System->setEnabled(true);
}

void MainWindow::UASSpecsChanged(int uas)
{
    UASInterface* activeUAS = UASManager::instance()->getActiveUAS();
    if (activeUAS)
    {
        if (activeUAS->getUASID() == uas)
        {
            ui.menuUnmanned_System->setTitle(activeUAS->getUASName());
        }
    }
}

void MainWindow::UASCreated(UASInterface* uas)
{

    // Connect the UAS to the full user interface

    if (uas != NULL)
    {
        // Set default settings
        setDefaultSettingsForAp();

        // The pilot, operator and engineer views were not available on startup, enable them now
        ui.actionPilotsView->setEnabled(true);
        ui.actionOperatorsView->setEnabled(true);
        ui.actionEngineersView->setEnabled(true);
        // The UAS actions are not enabled without connection to system
        ui.actionLiftoff->setEnabled(true);
        ui.actionLand->setEnabled(true);
        ui.actionEmergency_Kill->setEnabled(true);
        ui.actionEmergency_Land->setEnabled(true);
        ui.actionShutdownMAV->setEnabled(true);

        QIcon icon;
        // Set matching icon
        switch (uas->getSystemType())
        {
        case 0:
            icon = QIcon(":/images/mavs/generic.svg");
            break;
        case 1:
            icon = QIcon(":/images/mavs/fixed-wing.svg");
            break;
        case 2:
            icon = QIcon(":/images/mavs/quadrotor.svg");
            break;
        case 3:
            icon = QIcon(":/images/mavs/coaxial.svg");
            break;
        case 4:
            icon = QIcon(":/images/mavs/helicopter.svg");
            break;
        case 5:
            icon = QIcon(":/images/mavs/groundstation.svg");
            break;
        default:
            icon = QIcon(":/images/mavs/unknown.svg");
            break;
        }

        QAction* uasAction = new QAction(icon, tr("Select %1 for control").arg(uas->getUASName()), ui.menuConnected_Systems);
        connect(uas, SIGNAL(systemRemoved()), uasAction, SLOT(deleteLater()));
        connect(uasAction, SIGNAL(triggered()), uas, SLOT(setSelected()));
        connect(uas, SIGNAL(systemSpecsChanged(int)), this, SLOT(UASSpecsChanged(int)));

        ui.menuConnected_Systems->addAction(uasAction);

        // FIXME Should be not inside the mainwindow
        if (debugConsoleDockWidget)
        {
            DebugConsole *debugConsole = dynamic_cast<DebugConsole*>(debugConsoleDockWidget->widget());
            if (debugConsole)
            {
                connect(uas, SIGNAL(textMessageReceived(int,int,int,QString)),
                        debugConsole, SLOT(receiveTextMessage(int,int,int,QString)));
            }
        }

        // Health / System status indicator
        if (infoDockWidget)
        {
            UASInfoWidget *infoWidget = dynamic_cast<UASInfoWidget*>(infoDockWidget->widget());
            if (infoWidget)
            {
                infoWidget->addUAS(uas);
            }
        }

        // UAS List
        if (listDockWidget)
        {
            UASListWidget *listWidget = dynamic_cast<UASListWidget*>(listDockWidget->widget());
            if (listWidget)
            {
                listWidget->addUAS(uas);
            }
        }

        switch (uas->getAutopilotType())
        {
        case (MAV_AUTOPILOT_SLUGS):
            {
                // Build Slugs Widgets
                buildSlugsWidgets();

                // Connect Slugs Widgets
                connectSlugsWidgets();

                // Arrange Slugs Centerstack
                arrangeSlugsCenterStack();

                // Connect Slugs Actions
                connectSlugsActions();

                // FIXME: This type checking might be redundant
                //            if (slugsDataWidget) {
                //              SlugsDataSensorView* dataWidget = dynamic_cast<SlugsDataSensorView*>(slugsDataWidget->widget());
                //              if (dataWidget) {
                //                SlugsMAV* mav2 = dynamic_cast<SlugsMAV*>(uas);
                //                if (mav2) {
                (dynamic_cast<SlugsDataSensorView*>(slugsDataWidget->widget()))->addUAS(uas);
                //                  //loadSlugsView();
                //                  loadGlobalOperatorView();
                //                }
                //              }
                //            }

            }
            break;
        default:
        case (MAV_AUTOPILOT_GENERIC):
        case (MAV_AUTOPILOT_ARDUPILOTMEGA):
        case (MAV_AUTOPILOT_PIXHAWK):
            {
                // Build Pixhawk Widgets
                buildPxWidgets();

                // Connect Pixhawk Widgets
                connectPxWidgets();

                // Arrange Pixhawk Centerstack
                arrangePxCenterStack();

                // Connect Pixhawk Actions
                connectPxActions();
            }
            break;
        }

        // Change the view only if this is the first UAS

        // If this is the first connected UAS, it is both created as well as
        // the currently active UAS
        if (UASManager::instance()->getUASList().size() == 1)
        {
            // Load last view if setting is present
            if (settings.contains("CURRENT_VIEW_WITH_UAS_CONNECTED"))
            {
                clearView();
                int view = settings.value("CURRENT_VIEW_WITH_UAS_CONNECTED").toInt();
                switch (view)
                {
                case VIEW_ENGINEER:
                    loadEngineerView();
                    break;
                case VIEW_MAVLINK:
                    loadMAVLinkView();
                    break;
                case VIEW_PILOT:
                    loadPilotView();
                    break;
                case VIEW_UNCONNECTED:
                    loadUnconnectedView();
                    break;
                case VIEW_OPERATOR:
                default:
                    loadOperatorView();
                    break;
                }
            }
            else
            {
                loadOperatorView();
            }
        }

    }

    if (!ui.menuConnected_Systems->isEnabled()) ui.menuConnected_Systems->setEnabled(true);

    // Custom widgets, added last to all menus and layouts
    buildCustomWidget();
}

/**
 * Clears the current view completely
 */
void MainWindow::clearView()
{
    // Save current state
    if (UASManager::instance()->getUASList().count() > 0) settings.setValue(getWindowStateKey(), saveState(QGC::applicationVersion()));
    settings.setValue(getWindowGeometryKey(), saveGeometry());

    QAction* temp;

    // Set tool widget visibility settings for this view
    foreach (int key, toolsMenuActions.keys())
    {
        temp = toolsMenuActions[key];
        QString chKey = buildMenuKey (SUB_SECTION_CHECKED,static_cast<TOOLS_WIDGET_NAMES>(key), currentView);

        if (temp)
        {
            //qDebug() << "TOOL:" << chKey << "IS:" << temp->isChecked();
            settings.setValue(chKey,temp->isChecked());
        }
        else
        {
            //qDebug() << "TOOL:" << chKey << "IS DEFAULT AND UNCHECKED";
            settings.setValue(chKey,false);
        }
    }

    changingViewsFlag = true;
    // Remove all dock widgets from main window
    QObjectList childList( this->children() );

    QObjectList::iterator i;
    QDockWidget* dockWidget;
    for (i = childList.begin(); i != childList.end(); ++i)
    {
        dockWidget = dynamic_cast<QDockWidget*>(*i);
        if (dockWidget)
        {
            // Remove dock widget from main window
            removeDockWidget(dockWidget);
            dockWidget->hide();
            //dockWidget->setVisible(false);
        }
    }
    changingViewsFlag = false;
}

void MainWindow::loadEngineerView()
{
    if (currentView != VIEW_ENGINEER)
    {
        clearView();
        currentView = VIEW_ENGINEER;
        ui.actionEngineersView->setChecked(true);
        presentView();
    }
}

void MainWindow::loadOperatorView()
{
    if (currentView != VIEW_OPERATOR)
    {
        clearView();
        currentView = VIEW_OPERATOR;
        ui.actionOperatorsView->setChecked(true);
        presentView();
    }
}

void MainWindow::loadUnconnectedView()
{
    if (currentView != VIEW_UNCONNECTED)
    {
        clearView();
        currentView = VIEW_UNCONNECTED;
        ui.actionUnconnectedView->setChecked(true);
        presentView();
    }
}

void MainWindow::loadPilotView()
{
    if (currentView != VIEW_PILOT)
    {
        clearView();
        currentView = VIEW_PILOT;
        ui.actionPilotsView->setChecked(true);
        presentView();
    }
}

void MainWindow::loadMAVLinkView()
{
    if (currentView != VIEW_MAVLINK)
    {
        clearView();
        currentView = VIEW_MAVLINK;
        ui.actionMavlinkView->setChecked(true);
        presentView();
    }
}

void MainWindow::presentView()
{
    // LINE CHART
    showTheCentralWidget(CENTRAL_LINECHART, currentView);

    // MAP
    showTheCentralWidget(CENTRAL_MAP, currentView);

    // PROTOCOL
    showTheCentralWidget(CENTRAL_PROTOCOL, currentView);

    // HEAD UP DISPLAY
    showTheCentralWidget(CENTRAL_HUD, currentView);

    // GOOGLE EARTH
    showTheCentralWidget(CENTRAL_GOOGLE_EARTH, currentView);

    // LOCAL 3D VIEW
    showTheCentralWidget(CENTRAL_3D_LOCAL, currentView);

    // GLOBAL 3D VIEW
    showTheCentralWidget(CENTRAL_3D_MAP, currentView);

    // DATA PLOT
    showTheCentralWidget(CENTRAL_DATA_PLOT, currentView);


    // Show docked widgets based on current view and autopilot type

    // UAS CONTROL
    showTheWidget(MENU_UAS_CONTROL, currentView);

    // UAS LIST
    showTheWidget(MENU_UAS_LIST, currentView);

    // WAYPOINT LIST
    showTheWidget(MENU_WAYPOINTS, currentView);

    // UAS STATUS
    showTheWidget(MENU_STATUS, currentView);

    // DETECTION
    showTheWidget(MENU_DETECTION, currentView);

    // DEBUG CONSOLE
    showTheWidget(MENU_DEBUG_CONSOLE, currentView);

    // ONBOARD PARAMETERS
    showTheWidget(MENU_PARAMETERS, currentView);

    // WATCHDOG
    showTheWidget(MENU_WATCHDOG, currentView);

    // HUD
    showTheWidget(MENU_HUD, currentView);
    if (headUpDockWidget)
    {
        HUD* tmpHud = dynamic_cast<HUD*>( headUpDockWidget->widget() );
        if (tmpHud)
        {
            if (settings.value(buildMenuKey (SUB_SECTION_CHECKED,MENU_HUD,currentView)).toBool())
            {
                addDockWidget(static_cast <Qt::DockWidgetArea>(settings.value(buildMenuKey (SUB_SECTION_LOCATION,MENU_HUD, currentView)).toInt()),
                        headUpDockWidget);
                headUpDockWidget->show();
            }
            else
            {
                headUpDockWidget->hide();
            }
        }
    }


    // RC View
    showTheWidget(MENU_RC_VIEW, currentView);

    // SLUGS DATA
    showTheWidget(MENU_SLUGS_DATA, currentView);

    // SLUGS PID
    showTheWidget(MENU_SLUGS_PID, currentView);

    // SLUGS HIL
    showTheWidget(MENU_SLUGS_HIL, currentView);

    // SLUGS CAMERA
    showTheWidget(MENU_SLUGS_CAMERA, currentView);

    // HORIZONTAL SITUATION INDICATOR
    showTheWidget(MENU_HSI, currentView);

    // HEAD DOWN 1
    showTheWidget(MENU_HDD_1, currentView);

    // HEAD DOWN 2
    showTheWidget(MENU_HDD_2, currentView);

    // MAVLINK LOG PLAYER
    showTheWidget(MENU_MAVLINK_LOG_PLAYER, currentView);

    // VIDEO 1
    showTheWidget(MENU_VIDEO_STREAM_1, currentView);

    // VIDEO 2
    showTheWidget(MENU_VIDEO_STREAM_2, currentView);

    // Restore window state
    if (UASManager::instance()->getUASList().count() > 0)
    {
        // Restore the mainwindow size
        if (settings.contains(getWindowGeometryKey()))
        {
            restoreGeometry(settings.value(getWindowGeometryKey()).toByteArray());
        }

        // Restore the widget positions and size
        if (settings.contains(getWindowStateKey()))
        {
            restoreState(settings.value(getWindowStateKey()).toByteArray(), QGC::applicationVersion());
        }
    }

    // ACTIVATE MAP WIDGET
    if (headUpDockWidget)
    {
        HUD* tmpHud = dynamic_cast<HUD*>( headUpDockWidget->widget() );
        if (tmpHud)
        {

        }
    }

    this->show();
}

void MainWindow::showTheCentralWidget (TOOLS_WIDGET_NAMES centralWidget, VIEW_SECTIONS view)
{
    bool tempVisible;
    QWidget* tempWidget = dockWidgets[centralWidget];

    tempVisible =  settings.value(buildMenuKey(SUB_SECTION_CHECKED,centralWidget,view), false).toBool();
    //qDebug() << buildMenuKey (SUB_SECTION_CHECKED,centralWidget,view) << tempVisible;
    if (toolsMenuActions[centralWidget])
    {
        //qDebug() << "SETTING TO:" << tempVisible;
        toolsMenuActions[centralWidget]->setChecked(tempVisible);
    }

    if (centerStack && tempWidget && tempVisible)
    {
        //qDebug() << "ACTIVATING MAIN WIDGET";
        centerStack->setCurrentWidget(tempWidget);
    }
}

void MainWindow::loadDataView(QString fileName)
{
    clearView();

    // Unload line chart
    QString chKey = buildMenuKey (SUB_SECTION_CHECKED,static_cast<TOOLS_WIDGET_NAMES>(CENTRAL_LINECHART), currentView);
    settings.setValue(chKey,false);

    // Set data plot in settings as current widget and then run usual update procedure
    chKey = buildMenuKey (SUB_SECTION_CHECKED,static_cast<TOOLS_WIDGET_NAMES>(CENTRAL_DATA_PLOT), currentView);
    settings.setValue(chKey,true);

    presentView();

    // Plot is now selected, now load data from file
    if (dataplotWidget)
    {
        dataplotWidget->loadFile(fileName);
        }
//        QStackedWidget *centerStack = dynamic_cast<QStackedWidget*>(centralWidget());
//        if (centerStack)
//        {
//            centerStack->setCurrentWidget(dataplotWidget);
//            dataplotWidget->loadFile(fileName);
//        }
//    }
}


