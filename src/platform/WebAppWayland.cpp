// Copyright (c) 2014-2018 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include "WebAppWayland.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>

#include "ApplicationDescription.h"
#include "LogManager.h"
#include "WebAppWaylandWindow.h"
#include "WebPageBase.h"
#include "WindowTypes.h"

#include "WebPageBlink.h"

#include "webos/common/webos_constants.h"
#include "webos/window_group_configuration.h"

static int kLaunchFinishAssureTimeoutMs = 5000;

WebAppWayland::WebAppWayland(QString type,
                             int width, int height,
                             int displayId,
                             const std::string& location_hint)
    : WebAppBase()
    , m_appWindow(0)
    , m_windowType(type)
    , m_lastSwappedTime(0)
    , m_enableInputRegion(false)
    , m_isFocused(false)
    , m_vkbHeight(0)
    , m_lostFocusBySetWindowProperty(false)
    , m_displayId(displayId)
    , m_locationHint(location_hint)
{
    init(width, height);
}

WebAppWayland::WebAppWayland(QString type, WebAppWaylandWindow* window,
                             int width, int height,
                             int displayId,
                             const std::string& location_hint)
    : WebAppBase()
    , m_appWindow(window)
    , m_windowType(type)
    , m_lastSwappedTime(0)
    , m_enableInputRegion(false)
    , m_isFocused(false)
    , m_vkbHeight(0)
    , m_lostFocusBySetWindowProperty(false)
    , m_displayId(displayId)
    , m_locationHint(location_hint)
{
    init(width, height);
}

WebAppWayland::~WebAppWayland()
{
    delete m_appWindow;
}

static webos::WebAppWindowBase::LocationHint getLocationHintFromString(const std::string& value)
{
    std::map<std::string, webos::WebAppWindowBase::LocationHint> hints = {
        {"north", webos::WebAppWindowBase::LocationHint::kNorth},
        {"west", webos::WebAppWindowBase::LocationHint::kWest},
        {"south", webos::WebAppWindowBase::LocationHint::kSouth},
        {"east", webos::WebAppWindowBase::LocationHint::kEast},
        {"center", webos::WebAppWindowBase::LocationHint::kCenter},
        {"northwest", webos::WebAppWindowBase::LocationHint::kNorthWest},
        {"northeast", webos::WebAppWindowBase::LocationHint::kNorthEast},
        {"southwest", webos::WebAppWindowBase::LocationHint::kSouthWest},
        {"southeast", webos::WebAppWindowBase::LocationHint::kSouthEast}
    };

    webos::WebAppWindowBase::LocationHint hint = webos::WebAppWindowBase::LocationHint::kUnknown;
    if (hints.find(value) != hints.end()) {
        hint = hints[value];
    }
    return hint;
}

void WebAppWayland::init(int width, int height)
{
    if (!m_appWindow)
        m_appWindow = WebAppWaylandWindow::take();
    if (!(width && height)) {
        setUiSize(m_appWindow->DisplayWidth(), m_appWindow->DisplayHeight());
        m_appWindow->InitWindow(m_appWindow->DisplayWidth(), m_appWindow->DisplayHeight());
    }
    else {
        setUiSize(width, height);
        m_appWindow->InitWindow(width, height);
    }

    webos::WebAppWindowBase::LocationHint locationHint = getLocationHintFromString(m_locationHint);
    if (locationHint != webos::WebAppWindowBase::LocationHint::kUnknown) {
        m_appWindow->SetLocationHint(locationHint);
    }

    m_appWindow->setWebApp(this);

    // set compositor window type
    setWindowProperty(QStringLiteral("_WEBOS_WINDOW_TYPE"), m_windowType);
    LOG_DEBUG("App created window [%s]", qPrintable(m_windowType));

    if (m_displayId != kUndefinedDisplayId) {
      setWindowProperty(QStringLiteral("displayAffinity"), m_displayId);
      LOG_DEBUG("App window for display[%d]", m_displayId);
    }

    if (qgetenv("LAUNCH_FINISH_ASSURE_TIMEOUT").toInt() != 0)
        kLaunchFinishAssureTimeoutMs = qgetenv("LAUNCH_FINISH_ASSURE_TIMEOUT").toInt();

    if (!webos::WebOSPlatform::GetInstance()->GetInputPointer()) {
        // Create InputManager instance.
        InputManager::instance();
    }
}

void WebAppWayland::startLaunchTimer()
{
    if(!getHiddenWindow()) {
        LOG_DEBUG("APP_LAUNCHTIME_CHECK_STARTED [appId:%s]", qPrintable(appId()));
        m_elapsedLaunchTimer.start();
    }
}

void WebAppWayland::onDelegateWindowFrameSwapped()
{
    if(m_elapsedLaunchTimer.isRunning()) {
        m_lastSwappedTime = m_elapsedLaunchTimer.elapsed_ms();

        m_launchTimeoutTimer.stop();
        m_launchTimeoutTimer.start(kLaunchFinishAssureTimeoutMs,
                                   this,
                                   &WebAppWayland::onLaunchTimeout);
    }
}

void WebAppWayland::onLaunchTimeout()
{
    if(m_elapsedLaunchTimer.isRunning()) {
        m_launchTimeoutTimer.stop();
        m_elapsedLaunchTimer.stop();
        LOG_DEBUG("APP_LAUNCHTIME_CHECK_ALL_FRAMES_DONE [appId:%s time:%d]", qPrintable(appId()), m_lastSwappedTime);
    }
}

void WebAppWayland::forwardWebOSEvent(WebOSEvent* event) const
{
    page()->forwardEvent(event);
}

void WebAppWayland::attach(WebPageBase *page)
{
    WebAppBase::attach(page);

    setWindowProperty(QStringLiteral("appId"), appId());
    setWindowProperty(QStringLiteral("instanceId"), instanceId());
    setWindowProperty(QStringLiteral("launchingAppId"), launchingAppId());
    setWindowProperty(QStringLiteral("title"),
        QString::fromStdString(getAppDescription()->title()));
    setWindowProperty(QStringLiteral("icon"),
        QString::fromStdString(getAppDescription()->icon()));
    setWindowProperty(QStringLiteral("subtitle"), QStringLiteral(""));
    setWindowProperty(QStringLiteral("_WEBOS_WINDOW_CLASS"),
        QVariant((int)getAppDescription()->windowClassValue()));
    setWindowProperty(QStringLiteral("_WEBOS_ACCESS_POLICY_KEYS_BACK"),
                      getAppDescription()->backHistoryAPIDisabled()
                      ? QStringLiteral("true") : QStringLiteral("false"));
    setWindowProperty(QStringLiteral("_WEBOS_ACCESS_POLICY_KEYS_EXIT"),
                      getAppDescription()->handleExitKey()
                      ? QStringLiteral("true") : QStringLiteral("false"));
    setKeyMask(webos::WebOSKeyMask::KEY_MASK_BACK,
        getAppDescription()->backHistoryAPIDisabled());
    setKeyMask(webos::WebOSKeyMask::KEY_MASK_EXIT,
        getAppDescription()->handleExitKey());

    if (getAppDescription()->widthOverride() && getAppDescription()->heightOverride() && !getAppDescription()->isTransparent()) {
        float scaleX = static_cast<float>(m_appWindow->DisplayWidth()) / getAppDescription()->widthOverride();
        float scaleY = static_cast<float>(m_appWindow->DisplayHeight()) / getAppDescription()->heightOverride();
        m_scaleFactor = (scaleX < scaleY) ? scaleX : scaleY;
        static_cast<WebPageBlink*>(page)->setAdditionalContentsScale(scaleX, scaleY);
    }

    doAttach();

    static_cast<WebPageBlink*>(this->page())->setObserver(this);
}

WebPageBase* WebAppWayland::detach()
{
    static_cast<WebPageBlink*>(page())->setObserver(nullptr);
    return WebAppBase::detach();
}

void WebAppWayland::suspendAppRendering()
{
    onStageDeactivated();
    m_appWindow->hide();
}

void WebAppWayland::resumeAppRendering()
{
    m_appWindow->show();
    onStageActivated();
}

bool WebAppWayland::isFocused() const
{
    return m_isFocused;
}

void WebAppWayland::resize(int width, int height)
{
    m_appWindow->Resize(width, height);
}

bool WebAppWayland::isActivated() const
{
    return m_appWindow->GetWindowHostState() == webos::NATIVE_WINDOW_FULLSCREEN
        || m_appWindow->GetWindowHostState() == webos::NATIVE_WINDOW_MAXIMIZED
        || m_appWindow->GetWindowHostState() == webos::NATIVE_WINDOW_DEFAULT;
}

bool WebAppWayland::isMinimized()
{
    return m_appWindow->GetWindowHostState() == webos::NATIVE_WINDOW_MINIMIZED;
}

bool WebAppWayland::isNormal()
{
    return m_appWindow->GetWindowHostState() == webos::NATIVE_WINDOW_DEFAULT;
}

void WebAppWayland::onStageActivated()
{
    if (getCrashState()) {
        LOG_INFO(MSGID_WEBAPP_STAGE_ACITVATED, 4, PMLOGKS("APP_ID", qPrintable(appId())), PMLOGKS("INSTANCE_ID", qPrintable(instanceId())), PMLOGKFV("PID", "%d", page()->getWebProcessPID()), PMLOGKS("getCrashState()", "true; Reload default Page"), "");
        page()->reloadDefaultPage();
        setCrashState(false);
    }

    page()->resumeWebPageAll();

    page()->setVisibilityState(WebPageBase::WebPageVisibilityState::WebPageVisibilityStateVisible);

    setActiveInstanceId(instanceId());

    m_appWindow->show();

    LOG_INFO(MSGID_WEBAPP_STAGE_ACITVATED, 3, PMLOGKS("APP_ID", qPrintable(appId())), PMLOGKS("INSTANCE_ID", qPrintable(instanceId())), PMLOGKFV("PID", "%d", page()->getWebProcessPID()), "");
}

void WebAppWayland::onStageDeactivated()
{
    page()->suspendWebPageMedia();
    unfocus();
    page()->setVisibilityState(WebPageBase::WebPageVisibilityState::WebPageVisibilityStateHidden);
    page()->suspendWebPageAll();

    LOG_INFO(MSGID_WEBAPP_STAGE_DEACITVATED, 3, PMLOGKS("APP_ID", qPrintable(appId())), PMLOGKS("INSTANCE_ID", qPrintable(instanceId())), PMLOGKFV("PID", "%d", page()->getWebProcessPID()), "");
    m_didActivateStage = false;
}

void WebAppWayland::configureWindow(QString& type)
{
    m_windowType = type;
    m_appWindow->setWebApp(this);

    setWindowProperty(QStringLiteral("_WEBOS_WINDOW_TYPE"), type);
    setWindowProperty(QStringLiteral("appId"), appId());
    setWindowProperty(QStringLiteral("instanceId"), instanceId());
    setWindowProperty(QStringLiteral("launchingAppId"), launchingAppId());
    setWindowProperty(QStringLiteral("title"), QString::fromStdString(getAppDescription()->title()));
    setWindowProperty(QStringLiteral("icon"), QString::fromStdString(getAppDescription()->icon()));
    setWindowProperty(QStringLiteral("subtitle"), QStringLiteral(""));
    setWindowProperty(QStringLiteral("_WEBOS_WINDOW_CLASS"), QVariant((int)getAppDescription()->windowClassValue()));
    setWindowProperty(QStringLiteral("_WEBOS_ACCESS_POLICY_KEYS_BACK"),
                      getAppDescription()->backHistoryAPIDisabled()
                      ? QStringLiteral("true") : QStringLiteral("false"));
    setWindowProperty(QStringLiteral("_WEBOS_ACCESS_POLICY_KEYS_EXIT"),
                      getAppDescription()->handleExitKey()
                      ? QStringLiteral("true") : QStringLiteral("false"));
    setKeyMask(webos::WebOSKeyMask::KEY_MASK_BACK,
        getAppDescription()->backHistoryAPIDisabled());
    setKeyMask(webos::WebOSKeyMask::KEY_MASK_EXIT,
        getAppDescription()->handleExitKey());

    ApplicationDescription* appDesc = getAppDescription();
    if (!appDesc->groupWindowDesc().empty())
        setupWindowGroup(appDesc);

}

void WebAppWayland::setupWindowGroup(ApplicationDescription* desc)
{
    if (!desc)
        return;

    ApplicationDescription::WindowGroupInfo groupInfo = desc->getWindowGroupInfo();
    if (groupInfo.name.isEmpty())
        return;

    if (groupInfo.isOwner) {
        ApplicationDescription::WindowOwnerInfo ownerInfo = desc->getWindowOwnerInfo();
        webos::WindowGroupConfiguration config(groupInfo.name.toStdString());
        config.SetIsAnonymous(ownerInfo.allowAnonymous);
        QMap<QString, int>::iterator iter = ownerInfo.layers.begin();
        while (iter != ownerInfo.layers.end()){
          config.AddLayer(webos::WindowGroupLayerConfiguration(iter.key().toStdString(), iter.value()));
          iter++;
        }
        m_appWindow->CreateWindowGroup(config);
        LOG_INFO(MSGID_CREATE_SURFACEGROUP, 3, PMLOGKS("APP_ID", qPrintable(appId())), PMLOGKS("INSTANCE_ID", qPrintable(instanceId())), PMLOGKFV("PID", "%d", page()->getWebProcessPID()), "");
    } else {
        ApplicationDescription::WindowClientInfo clientInfo = desc->getWindowClientInfo();
        m_appWindow->AttachToWindowGroup(groupInfo.name.toStdString(), clientInfo.layer.toStdString());
        LOG_INFO(MSGID_ATTACH_SURFACEGROUP, 4, PMLOGKS("APP_ID", qPrintable(appId())), PMLOGKS("OWNER_ID", qPrintable(groupInfo.name)), PMLOGKS("INSTANCE_ID", qPrintable(instanceId())), PMLOGKFV("PID", "%d", page()->getWebProcessPID()), "");
    }
}

bool WebAppWayland::isKeyboardVisible()
{
    return m_appWindow->IsKeyboardVisible();
}

void WebAppWayland::setKeyMask(webos::WebOSKeyMask keyMask, bool value)
{
    m_appWindow->SetKeyMask(keyMask, value);
}

void WebAppWayland::applyInputRegion()
{
    if (!m_enableInputRegion && !m_inputRegion.empty()) {
        m_enableInputRegion = true;
        m_appWindow->SetInputRegion(m_inputRegion);
    }
}

void WebAppWayland::setInputRegion(const QJsonDocument& jsonDoc)
{
    m_inputRegion.clear();

    if (jsonDoc.isArray()) {
        QJsonArray jsonArray = jsonDoc.array();

        for (int i = 0; i < jsonArray.size(); i++) {
            QVariantMap map = jsonArray[i].toObject().toVariantMap();
            m_inputRegion.push_back(gfx::Rect(
                                map["x"].toInt() * m_scaleFactor,
                                map["y"].toInt() * m_scaleFactor,
                                map["width"].toInt() * m_scaleFactor,
                                map["height"].toInt() * m_scaleFactor));
        }
    }

    m_appWindow->SetInputRegion(m_inputRegion);
}


void WebAppWayland::setWindowProperty(const QString& name, const QVariant& value)
{
    webos::WebOSKeyMask mask = static_cast<webos::WebOSKeyMask>(0);
    if (name == "_WEBOS_ACCESS_POLICY_KEYS_BACK")
        mask = webos::WebOSKeyMask::KEY_MASK_BACK;
    else if (name == "_WEBOS_ACCESS_POLICY_KEYS_EXIT")
        mask = webos::WebOSKeyMask::KEY_MASK_EXIT;
    // if mask is not set, not need to call setKeyMask
    if (mask != static_cast<webos::WebOSKeyMask>(0))
        setKeyMask(mask, value.toBool());
    m_appWindow->SetWindowProperty(name.toStdString(), value.toString().toStdString());
}

void WebAppWayland::platformBack()
{
    m_appWindow->platformBack();
}

void WebAppWayland::setCursor(const QString& cursorArg, int hotspot_x, int hotspot_y)
{
    m_appWindow->setCursor(cursorArg, hotspot_x, hotspot_y);
}

static QMap<QString, webos::WebOSKeyMask>& getKeyMaskTable()
{
    static QMap<QString, webos::WebOSKeyMask> mapTable;

    if (mapTable.isEmpty()) {
        mapTable["KeyMaskNone"]      = static_cast<webos::WebOSKeyMask>(0);
        mapTable["KeyMaskHome"]      = webos::WebOSKeyMask::KEY_MASK_HOME;
        mapTable["KeyMaskBack"]      = webos::WebOSKeyMask::KEY_MASK_BACK;
        mapTable["KeyMaskExit"]      = webos::WebOSKeyMask::KEY_MASK_EXIT;
        mapTable["KeyMaskLeft"]      = webos::WebOSKeyMask::KEY_MASK_LEFT;
        mapTable["KeyMaskRight"]     = webos::WebOSKeyMask::KEY_MASK_RIGHT;
        mapTable["KeyMaskUp"]        = webos::WebOSKeyMask::KEY_MASK_UP;
        mapTable["KeyMaskDown"]      = webos::WebOSKeyMask::KEY_MASK_DOWN;
        mapTable["KeyMaskOk"]        = webos::WebOSKeyMask::KEY_MASK_OK;
        mapTable["KeyMaskNumeric"]   = webos::WebOSKeyMask::KEY_MASK_NUMERIC;
        mapTable["KeyMaskRed"]       = webos::WebOSKeyMask::KEY_MASK_REMOTECOLORRED;
        mapTable["KeyMaskGreen"]     = webos::WebOSKeyMask::KEY_MASK_REMOTECOLORGREEN;
        mapTable["KeyMaskYellow"]    = webos::WebOSKeyMask::KEY_MASK_REMOTECOLORYELLOW;
        mapTable["KeyMaskBlue"]      = webos::WebOSKeyMask::KEY_MASK_REMOTECOLORBLUE;
        mapTable["KeyMaskProgramme"] = webos::WebOSKeyMask::KEY_MASK_REMOTEPROGRAMMEGROUP;
        mapTable["KeyMaskPlayback"]  = webos::WebOSKeyMask::KEY_MASK_REMOTEPLAYBACKGROUP;
        mapTable["KeyMaskTeletext"]  = webos::WebOSKeyMask::KEY_MASK_REMOTETELETEXTGROUP;
        mapTable["KeyMaskDefault"]   = webos::WebOSKeyMask::KEY_MASK_DEFAULT;
    }

    return mapTable;
}

void WebAppWayland::setKeyMask(const QJsonDocument& jsonDoc)
{
    static QMap<QString, webos::WebOSKeyMask>& mapTable = getKeyMaskTable();
    unsigned int keyMask = 0;

    if (jsonDoc.isArray()) {
        QJsonArray jsonArray = jsonDoc.array();

        for (int i = 0; i < jsonArray.size(); i++)
            keyMask |= mapTable.value(jsonArray[i].toString());
    }

    m_appWindow->SetKeyMask(static_cast<webos::WebOSKeyMask>(keyMask));
}

void WebAppWayland::setKeyMask(webos::WebOSKeyMask keyMask)
{
    m_appWindow->SetKeyMask(keyMask);
}

void WebAppWayland::focusOwner()
{
    m_appWindow->FocusWindowGroupOwner();
    LOG_DEBUG("FocusOwner [%s]", qPrintable(appId()));
}

void WebAppWayland::focusLayer()
{
    m_appWindow->FocusWindowGroupLayer();
    ApplicationDescription * desc = getAppDescription();
    if (desc) {
        ApplicationDescription::WindowClientInfo clientInfo = desc->getWindowClientInfo();
        LOG_DEBUG("FocusLayer(layer:%s) [%s]",qPrintable(clientInfo.layer) ,qPrintable(appId()));
    }
}

void WebAppWayland::setOpacity(float opacity)
{
    m_appWindow->SetOpacity(opacity);
}

void WebAppWayland::hide(bool forcedHide)
{
    if (keepAlive() || forcedHide) {
        onStageDeactivated();
        m_appWindow->hide();
        setHiddenWindow(true);
    }
}

void WebAppWayland::focus()
{
    m_isFocused = true;
    if(!isMinimized())
        page()->setFocus(true);
}

void WebAppWayland::unfocus()
{
    m_isFocused = false;
    page()->setFocus(false);
}

void WebAppWayland::doAttach()
{
    // Do App and window things
    ApplicationDescription* appDesc = getAppDescription();
    if (!appDesc->groupWindowDesc().empty())
        setupWindowGroup(appDesc);

    m_appWindow->attachWebContents(page()->getWebContents());
    // The attachWebContents causes visibilityState change to Visible (by default, init)
    // And now, should update the visibilityState to launching
    page()->setVisibilityState(WebPageBase::WebPageVisibilityState::WebPageVisibilityStateLaunching);

    // Do Page things
    page()->setPageProperties();

    if (keepAlive())
        page()->setKeepAliveWebApp(keepAlive());

    connect(page(), SIGNAL(webPageClosePageRequested()), this, SLOT(webPageClosePageRequestedSlot()));
    connect(page(), SIGNAL(webViewRecreated()), this, SLOT(webViewRecreatedSlot()));
}

void WebAppWayland::raise()
{
    bool wasMinimizedState = isMinimized();

    //There's no fullscreen event from LSM for below cases, so onStageActivated should be called
    //1. When overlay window is raised
    //2. When there's only one keepAlive app, and this keepAlive app is closed and is shown again
    if ((getWindowType() == WT_OVERLAY) || (keepAlive() && !wasMinimizedState)) {
        LOG_INFO(MSGID_WAM_DEBUG, 3, PMLOGKS("APP_ID", qPrintable(appId())), PMLOGKS("INSTANCE_ID", qPrintable(instanceId())), PMLOGKFV("PID", "%d", page()->getWebProcessPID()), "WebAppWayland::raise(); call onStageActivated");
        onStageActivated();
    } else {
        LOG_INFO(MSGID_WAM_DEBUG, 3, PMLOGKS("APP_ID", qPrintable(appId())), PMLOGKS("INSTANCE_ID", qPrintable(instanceId())), PMLOGKFV("PID", "%d", page()->getWebProcessPID()), "WebAppWayland::raise(); call setWindowState(webos::NATIVE_WINDOW_FULLSCREEN)");
        m_appWindow->SetWindowHostState(webos::NATIVE_WINDOW_FULLSCREEN);
    }

    if (wasMinimizedState)
        page()->setVisibilityState(WebPageBase::WebPageVisibilityState::WebPageVisibilityStateVisible);
}

void WebAppWayland::goBackground()
{
    if (getWindowType() == WT_OVERLAY) {
        LOG_INFO(MSGID_WAM_DEBUG, 3, PMLOGKS("APP_ID", qPrintable(appId())), PMLOGKS("INSTANCE_ID", qPrintable(instanceId())), PMLOGKFV("PID", "%d", page()->getWebProcessPID()), "WebAppWayland::goBackground(); windowType:OVERLAY; Try close; call doClose()");
        doClose();
    } else {
        LOG_INFO(MSGID_WAM_DEBUG, 3, PMLOGKS("APP_ID", qPrintable(appId())), PMLOGKS("INSTANCE_ID", qPrintable(instanceId())), PMLOGKFV("PID", "%d", page()->getWebProcessPID()), "WebAppWayland::goBackground(); call setWindowState(webos::NATIVE_WINDOW_MINIMIZED)");
        m_appWindow->SetWindowHostState(webos::NATIVE_WINDOW_MINIMIZED);
    }
}

void WebAppWayland::webPageLoadFinishedSlot()
{
    if (getHiddenWindow())
        return;
    if(needReload()) {
        page()->reload();
        setNeedReload(false);
        return;
    }

    doPendingRelaunch();
}

void WebAppWayland::webPageLoadFailedSlot(int errorCode)
{
    // Do not load error page while preoload app launching.
    if (preloadState() != NONE_PRELOAD)
        closeAppInternal();
}

void WebAppWayland::doClose()
{
    if (forceClose()) {
        LOG_INFO(MSGID_WAM_DEBUG, 3, PMLOGKS("APP_ID", qPrintable(appId())), PMLOGKS("INSTANCE_ID", qPrintable(instanceId())), PMLOGKFV("PID", "%d", page()->getWebProcessPID()), "WebAppWayland::doClose(); forceClose() TRUE; call forceCloseAppInternal() and return");
        forceCloseAppInternal();
        return;
    }

    if (keepAlive() && hideWindow())
        return;

    LOG_INFO(MSGID_WAM_DEBUG, 3, PMLOGKS("APP_ID", qPrintable(appId())), PMLOGKS("INSTANCE_ID", qPrintable(instanceId())), PMLOGKFV("PID", "%d", page()->getWebProcessPID()), "WebAppWayland::doClose(); call closeAppInternal()");
    closeAppInternal();
}

void WebAppWayland::stateAboutToChange(webos::NativeWindowState willBe)
{
    if (willBe == webos::NATIVE_WINDOW_MINIMIZED) {
        LOG_INFO(MSGID_WAM_DEBUG, 3, PMLOGKS("APP_ID", qPrintable(appId())), PMLOGKS("INSTANCE_ID", qPrintable(instanceId())), PMLOGKFV("PID", "%d", page()->getWebProcessPID()), "WebAppWayland::stateAboutToChange; will be Minimized; suspend media and fire visibilitychange event");
        page()->suspendWebPageMedia();
        page()->setVisibilityState(WebPageBase::WebPageVisibilityState::WebPageVisibilityStateHidden);
    }
}

void WebAppWayland::stateChanged(webos::NativeWindowState newState)
{
    if (isClosing()) {
        LOG_INFO(MSGID_WINDOW_STATE_CHANGED, 1, PMLOGKS("APP_ID", qPrintable(appId())), "In Closing; return;");
        return;
    }

    switch (newState)
    {
        case webos::NATIVE_WINDOW_DEFAULT:
        case webos::NATIVE_WINDOW_MAXIMIZED:
        case webos::NATIVE_WINDOW_FULLSCREEN:
            LOG_INFO(MSGID_WINDOW_STATE_CHANGED, 1, PMLOGKS("APP_ID", qPrintable(appId())), "To FullScreen; call onStageActivated");
            applyInputRegion();
            onStageActivated();
            break;
        case webos::NATIVE_WINDOW_MINIMIZED:
            LOG_INFO(MSGID_WINDOW_STATE_CHANGED, 1, PMLOGKS("APP_ID", qPrintable(appId())), "To Minimized; call onStageDeactivated");
            onStageDeactivated();
            break;
        default:
            LOG_INFO(MSGID_WINDOW_STATE_CHANGED, 2, PMLOGKS("APP_ID", qPrintable(appId())), PMLOGKFV("HOST_STATE", "%d", newState), "Unknown state. Do not calling nothing anymore.");
            break;
    }
}

void WebAppWayland::showWindow()
{
    if (m_preloadState != NONE_PRELOAD) {
        LOG_INFO(MSGID_WAM_DEBUG, 3, PMLOGKS("APP_ID", qPrintable(appId())), PMLOGKS("INSTANCE_ID", qPrintable(instanceId())), PMLOGKFV("PID", "%d", page()->getWebProcessPID()), "WebAppWayland::showWindow(); But Preloaded app; return");
        return;
    }

    setHiddenWindow(false);

    onStageActivated();
    m_addedToWindowMgr = true;
    WebAppBase::showWindow();
}

bool WebAppWayland::hideWindow()
{
    if (page()->isLoadErrorPageFinish())
        return false;

    LOG_INFO(MSGID_WAM_DEBUG, 3, PMLOGKS("APP_ID", qPrintable(appId())), PMLOGKS("INSTANCE_ID", qPrintable(instanceId())), PMLOGKFV("PID", "%d", page()->getWebProcessPID()), "WebAppWayland::hideWindow(); just hide this app");
    page()->closeVkb();
    hide(true);
    m_addedToWindowMgr = false;
    return true;
}


void WebAppWayland::showWindowSlot()
{
    showWindow();
}

void WebAppWayland::titleChanged()
{
    setWindowProperty(QStringLiteral("subtitle"), page()->title());
}

void WebAppWayland::firstFrameVisuallyCommitted()
{
    LOG_INFO(MSGID_WAM_DEBUG, 3, PMLOGKS("APP_ID", qPrintable(appId())), PMLOGKS("INSTANCE_ID", qPrintable(instanceId())), PMLOGKFV("PID", "%d", page()->getWebProcessPID()), "firstFrameVisuallyCommitted");
    // if m_preloadState != NONE_PRELOAD, then we must ignore the first frame commit
    // if getHiddenWindow() == true, then we have specifically requested that the window is to be hidden,
    // and therefore we have to do an explicit show
    if (!getHiddenWindow() && m_preloadState == NONE_PRELOAD) {
        LOG_INFO(MSGID_WAM_DEBUG, 3, PMLOGKS("APP_ID", qPrintable(appId())), PMLOGKS("INSTANCE_ID", qPrintable(instanceId())), PMLOGKFV("PID", "%d", page()->getWebProcessPID()), "Not hidden window, preload, call showWindow");
        if (getAppDescription()->usePrerendering())
            m_didActivateStage = false;
        showWindow();
    }
}

void WebAppWayland::postEvent(WebOSEvent* ev)
{
    m_appWindow->event(ev);
}

void WebAppWayland::navigationHistoryChanged()
{
    if (!getAppDescription()->backHistoryAPIDisabled()) {
        // if backHistoryAPIDisabled is true, no chance to change this value
        setWindowProperty(QStringLiteral("_WEBOS_ACCESS_POLICY_KEYS_BACK"),
                          page()->canGoBack() ?
                          QStringLiteral("true") : /* send next back key to WAM */
                          QStringLiteral("false")); /* Do not send back key to WAM. LSM should handle it */
    }
}

void WebAppWayland::webViewRecreatedSlot()
{
    m_appWindow->attachWebContents(page()->getWebContents());
    m_appWindow->RecreatedWebContents();
    page()->setPageProperties();
    if (keepAlive())
        page()->setKeepAliveWebApp(keepAlive());
    focus();
}

void WebAppWayland::didSwapPageCompositorFrame()
{
    if (!m_didActivateStage && !getHiddenWindow() && m_preloadState == NONE_PRELOAD) {
        LOG_INFO(MSGID_WAM_DEBUG, 2, PMLOGKS("APP_ID", qPrintable(appId())), PMLOGKFV("PID", "%d", page()->getWebProcessPID()), "Not hidden window, preload, activate stage");
        onStageActivated();
        m_didActivateStage = true;
    }
}

void WebAppWayland::didResumeDOM()
{
    focus();
}

void InputManager::OnCursorVisibilityChanged(bool visible)
{
    if (IsVisible() == visible) return;

    LOG_DEBUG("InputManager::onCursorVisibilityChanged; Global Cursor visibility Changed to %s; send cursorStateChange event to all app, all frames", visible? "true" : " false");
    SetVisible(visible);
    // send event about  cursorStateChange
    QString cursorStateChangeEvt = QStringLiteral(
        "    var cursorEvent=new CustomEvent('cursorStateChange', { detail: { 'visibility' : %1 } });"
        "    cursorEvent.visibility = %2;"
        "    if(document) document.dispatchEvent(cursorEvent);"
    ).arg(visible ? "true" : "false"). arg(visible ? "true" : "false");

    // send javascript event : cursorStateChange with param to All app
    // if javascript has setTimeout() like webOSlaunch or webOSRelaunch, then app can not get this event when app is in background
    // because javascript is freezed and timer is too, since app is in background, timer is never fired
    WebAppBase::onCursorVisibilityChanged(cursorStateChangeEvt);
}

void WebAppWayland::sendWebOSMouseEvent(const QString& eventName)
{
    if (eventName == "Enter" || eventName == "Leave") {
        // send webOSMouse event to app
        QString javascript = QStringLiteral(
            "console.log('[WAM] fires webOSMouse event : %1');"
            "var mouseEvent =new CustomEvent('webOSMouse', { detail: { type : '%2' }});"
            "document.dispatchEvent(mouseEvent);").arg(eventName).arg(eventName);
        LOG_DEBUG("[%s] WebAppWayland::sendWebOSMouseEvent; dispatch webOSMouse; %s", qPrintable(appId()), qPrintable(eventName));
        page()->evaluateJavaScript(javascript);
    }
}

void WebAppWayland::deleteSurfaceGroup()
{
    m_appWindow->DetachWindowGroup();
}

void WebAppWayland::setKeepAlive(bool keepAlive)
{
    WebAppBase::setKeepAlive(keepAlive);
    if (page())
        page()->setKeepAliveWebApp(keepAlive);
}

void WebAppWayland::moveInputRegion(int height)
{
    if (!m_enableInputRegion)
        return;

    if (height)
        m_vkbHeight = height;
    else
        m_vkbHeight = -m_vkbHeight;

    std::vector<gfx::Rect> newRegion;
    for (std::vector<gfx::Rect>::iterator it = m_inputRegion.begin(); it != m_inputRegion.end(); ++it) {
        gfx::Rect rect = static_cast<gfx::Rect>(*it);
        rect.SetRect(rect.x(),
                     rect.y() - m_vkbHeight,
                     rect.width(),
                     rect.height());
        newRegion.push_back(rect);
    }
    m_inputRegion.clear();
    m_inputRegion = newRegion;
    m_appWindow->SetInputRegion(m_inputRegion);
}

void WebAppWayland::keyboardVisibilityChanged(bool visible, int height) {
    WebAppBase::keyboardVisibilityChanged(visible, height);
    moveInputRegion(height);
}

void WebAppWayland::setUseVirtualKeyboard(const bool enable)
{
    m_appWindow->SetUseVirtualKeyboard(enable);
}
