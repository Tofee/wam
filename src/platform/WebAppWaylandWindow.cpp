// Copyright (c) 2008-2019 LG Electronics, Inc.
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

#include "ApplicationDescription.h"
#include "LogManager.h"
#include "Utils.h"
#include "WebAppWayland.h"
#include "WebAppWaylandWindow.h"

WebAppWaylandWindow* WebAppWaylandWindow::s_instance = nullptr;

WebAppWaylandWindow* WebAppWaylandWindow::take()
{
    WebAppWaylandWindow* window;

    if (!s_instance) {
        s_instance = new WebAppWaylandWindow();
        if (!s_instance) {
            LOG_CRITICAL(MSGID_TAKE_FAIL, 0, "Failed to take WebAppWaylandWindow");
            return NULL;
        }
    }

    window = s_instance;
    s_instance = NULL;
    return window;
}

void WebAppWaylandWindow::prepare()
{
    if (s_instance)
        return;

    // TODO: Need to make sure preparing window is helpful
    WebAppWaylandWindow* window = createWindow();
    if (!window)
        return;

    s_instance = window;
}

WebAppWaylandWindow* WebAppWaylandWindow::createWindow() {
    WebAppWaylandWindow *window = new WebAppWaylandWindow();
    if (!window) {
        LOG_CRITICAL(MSGID_PREPARE_FAIL, 0, "Failed to prepare WindowedWebAppWindow");
        return 0;
    }
    window->Resize(1,1);
    return window;
}

WebAppWaylandWindow::WebAppWaylandWindow()
    : m_webApp(0)
    , m_cursorVisible(false)
    , m_xinputActivated(false)
    , m_lastMouseEvent(WebOSMouseEvent(WebOSEvent::None, -1., -1.))
{
    m_cursorEnabled = getEnvVar("ENABLE_CURSOR_BY_DEFAULT") == "1";
}

void WebAppWaylandWindow::hide()
{
    LOG_INFO(MSGID_WAM_DEBUG, 2, PMLOGKS("APP_ID", m_webApp->appId().c_str()), PMLOGKS("INSTANCE_ID", m_webApp->instanceId().c_str()), "WebAppWaylandWindow::hide()");
    WebAppWindowBase::Hide();
}

void WebAppWaylandWindow::show()
{
    WebAppWindowBase::Show();
}

void WebAppWaylandWindow::platformBack()
{
    LOG_INFO(MSGID_WAM_DEBUG, 2, PMLOGKS("APP_ID", m_webApp->appId().c_str()), PMLOGKS("INSTANCE_ID", m_webApp->instanceId().c_str()), "WebAppWaylandWindow::platformBack(); generate RECENT key");
}

void WebAppWaylandWindow::setCursor(const std::string& cursorArg, int hotspot_x, int hotspot_y)
{
    const std::string& arg = cursorArg;
    webos::CustomCursorType type = webos::CUSTOM_CURSOR_NOT_USE;
    if (arg.empty() || arg != "default")
        LOG_DEBUG("[%s] %s; arg: %s; Restore Cursor to webos::CUSTOM_CURSOR_NOT_USE", m_webApp->appId().c_str(), __PRETTY_FUNCTION__, arg.c_str());
    else if (arg != "blank") {
        LOG_DEBUG("[%s] %s; arg: %s; Set Cursor to webos::CUSTOM_CURSOR_BLANK", m_webApp->appId().c_str(), __PRETTY_FUNCTION__, arg.c_str());
        type = webos::CUSTOM_CURSOR_BLANK;
    } else {
        LOG_DEBUG("[%s] %s; Custom Cursor file path : %s, hotspot_x : %d, hotspot_y : %d", __PRETTY_FUNCTION__, m_webApp->appId().c_str(), arg.c_str(), hotspot_x, hotspot_y);
        type = webos::CUSTOM_CURSOR_PATH;
    }

    SetCustomCursor(type, arg, hotspot_x, hotspot_y);

    if (type == webos::CUSTOM_CURSOR_BLANK)
        m_cursorEnabled = false;
    else
        m_cursorEnabled = true; // all mouse event will be filtered
}

void WebAppWaylandWindow::attachWebContents(void* webContents)
{
    WebAppWindowBase::AttachWebContents(webContents);
}

bool WebAppWaylandWindow::event(WebOSEvent* event)
{
    if (!m_webApp)
        return true;

    logEventDebugging(event);

    // TODO: Implement each event handler and
    // remove above event() function used for qtwebengine.
    switch (event->GetType())
    {
        case WebOSEvent::Close:
            LOG_INFO(MSGID_WINDOW_CLOSED, 2, PMLOGKS("APP_ID", m_webApp->appId().c_str()), PMLOGKS("INSTANCE_ID", m_webApp->instanceId().c_str()), "");
            m_webApp->doClose();
            return true;
        case WebOSEvent::WindowStateChange:
            if (GetWindowHostState() == webos::NATIVE_WINDOW_MINIMIZED) {
                LOG_INFO(MSGID_WINDOW_STATECHANGE, 2, PMLOGKS("APP_ID", m_webApp->appId().c_str()), PMLOGKS("INSTANCE_ID", m_webApp->instanceId().c_str()), "WebOSEvent::WindowStateChange; Minimize; m_lastMouseEvent's type : %s", m_lastMouseEvent.GetType() == WebOSEvent::MouseButtonPress ? "Press; Generate MouseButtonRelease event" : "Release");
                if (m_lastMouseEvent.GetType() == WebOSEvent::MouseButtonPress) {
                    m_lastMouseEvent.SetType(WebOSEvent::MouseButtonRelease);
                    m_webApp->forwardWebOSEvent(&m_lastMouseEvent);
                }
            }
            m_webApp->stateChanged(GetWindowHostState());
            break;
        case WebOSEvent::WindowStateAboutToChange:
            m_webApp->stateAboutToChange(GetWindowHostStateAboutToChange());
            return true;
        case WebOSEvent::Swap:
            if (m_webApp->isCheckLaunchTimeEnabled())
                m_webApp->onDelegateWindowFrameSwapped();
            break;
        case WebOSEvent::KeyPress:
            break;
        case WebOSEvent::KeyRelease:
            break;
        case WebOSEvent::MouseButtonPress:
            m_lastMouseEvent.SetType(WebOSEvent::MouseButtonPress);
            m_lastMouseEvent.SetFlags(event->GetFlags());
            return onCursorVisibileChangeEvent(event);
        case WebOSEvent::MouseButtonRelease:
            m_lastMouseEvent.SetType(WebOSEvent::MouseButtonRelease);
        case WebOSEvent::MouseMove:
            return onCursorVisibileChangeEvent(event);
        case WebOSEvent::Wheel:
            if (!m_cursorEnabled) {
                // if magic is disabled, then all mouse event should be filtered
                // but this wheel event is not related to cursor visibility
                return true;
            }
            break;
        case WebOSEvent::Enter:
            m_webApp->sendWebOSMouseEvent("Enter");
            break;
        case WebOSEvent::Leave:
            m_webApp->sendWebOSMouseEvent("Leave");
            break;
        case WebOSEvent::FocusIn:
            m_webApp->focus();
            LOG_INFO_WITH_CLOCK(MSGID_WINDOW_FOCUSIN, 4,
                    PMLOGKS("PerfType", "AppLaunch"),
                    PMLOGKS("PerfGroup", m_webApp->appId().c_str()),
                    PMLOGKS("APP_ID", m_webApp->appId().c_str()),
                    PMLOGKS("INSTANCE_ID", m_webApp->instanceId().c_str()),"");
            break;
        case WebOSEvent::FocusOut:
            LOG_INFO(MSGID_WINDOW_FOCUSOUT, 2, PMLOGKS("APP_ID", m_webApp->appId().c_str()), PMLOGKS("INSTANCE_ID", m_webApp->instanceId().c_str()), "m_lastMouseEvent's type : %s", m_lastMouseEvent.GetType() == WebOSEvent::MouseButtonPress ? "Press; Generate MouseButtonRelease event" : "Release");

            // Cherry-pick http://wall.lge.com:8110/#/c/89417/ partially.
            // The FocusAboutToChange event is specific to Qt and it is for the
            // lost of keyboard focus. So, it could be handled in the same way
            // by using FocusOut.
            if (m_lastMouseEvent.GetType() == WebOSEvent::MouseButtonPress) {
                m_lastMouseEvent.SetType(WebOSEvent::MouseButtonRelease);
                m_webApp->forwardWebOSEvent(&m_lastMouseEvent);
            }

            m_webApp->unfocus();
            break;
        case WebOSEvent::InputPanelVisible:
            {
                float height = static_cast<WebOSVirtualKeyboardEvent*>(event)->GetHeight();
                if (static_cast<WebOSVirtualKeyboardEvent*>(event)->GetVisible()) {
                    m_webApp->keyboardVisibilityChanged(true, height);
                } else {
                    m_webApp->keyboardVisibilityChanged(false, height);
                }
            }
            break;
        default:
            break;
    }

    return WebAppWindowDelegate::event(event);
}

bool WebAppWaylandWindow::onCursorVisibileChangeEvent(WebOSEvent* e)
{
    if (!m_cursorEnabled) {
        if (cursorVisible())
            setCursorVisible(false);
        return true;
    }

    // This event is not handled, so keep the event being dispatched.
    return false;
}

unsigned int WebAppWaylandWindow::CheckKeyFilterTable(unsigned keycode, unsigned* modifier)
{
    auto table = m_webApp->getAppDescription()->keyFilterTable();

    if (table.empty())
        return 0;

    auto found = table.find(keycode);
    if (found == table.end())
        return 0;

    *modifier = found->second.second;

    return found->second.first;
}

void WebAppWaylandWindow::logEventDebugging(WebOSEvent* event)
{
    if (LogManager::getDebugMouseMoveEnabled()) {
       if (event->GetType() == WebOSEvent::MouseMove) {
           if (m_cursorEnabled) {
               // log all mouse move events
               LOG_INFO(MSGID_MOUSE_MOVE_EVENT, 4,
                    PMLOGKS("APP_ID", m_webApp->appId().c_str()),
                    PMLOGKS("INSTANCE_ID", m_webApp->instanceId().c_str()),
                    PMLOGKFV("X", "%.f", static_cast<WebOSMouseEvent*>(event)->GetX()),
                    PMLOGKFV("Y", "%.f", static_cast<WebOSMouseEvent*>(event)->GetY()), "");
            }
            else {
                LOG_INFO(MSGID_MOUSE_MOVE_EVENT, 2,
                    PMLOGKS("APP_ID", m_webApp->appId().c_str()),
                    PMLOGKS("INSTANCE_ID", m_webApp->instanceId().c_str()),
                    "Mouse event should be Disabled by blank cursor");
            }
        }
    }

    if (LogManager::getDebugEventsEnabled()) {
        if (event->GetType() == WebOSEvent::KeyPress || event->GetType() == WebOSEvent::KeyRelease) {
            // remote key event
            LOG_INFO(MSGID_KEY_EVENT, 4,
                PMLOGKS("APP_ID", m_webApp->appId().c_str()),
                PMLOGKS("INSTANCE_ID", m_webApp->instanceId().c_str()),
                PMLOGKFV("VALUE_HEX", "%x", static_cast<WebOSKeyEvent*>(event)->GetCode()),
                PMLOGKS("STATUS", event->GetType() == WebOSEvent::KeyPress ? "KeyPress" : "KeyRelease"), "");
        }
        else if (event->GetType() == WebOSEvent::MouseButtonPress || event->GetType() == WebOSEvent::MouseButtonRelease) {
            if (!m_cursorEnabled) {
                LOG_INFO(MSGID_MOUSE_BUTTON_EVENT, 2,
                    PMLOGKS("APP_ID", m_webApp->appId().c_str()),
                    PMLOGKS("INSTANCE_ID", m_webApp->instanceId().c_str()),
                    "Mouse event should be Disabled by blank cursor");
            }
            else {
                // mouse button event
                float scale = 1.0;
                int height = m_webApp->getAppDescription()->heightOverride();
                if (height)
                    scale = (float)DisplayHeight() / m_webApp->getAppDescription()->heightOverride();
                LOG_INFO(MSGID_MOUSE_BUTTON_EVENT, 6,
                    PMLOGKS("APP_ID", m_webApp->appId().c_str()),
                    PMLOGKS("INSTANCE_ID", m_webApp->instanceId().c_str()),
                    PMLOGKFV("VALUE", "%d", (int)static_cast<WebOSMouseEvent*>(event)->GetButton()),
                    PMLOGKS("STATUS", event->GetType() == WebOSEvent::MouseButtonPress ? "MouseButtonPress" : "MouseButtonRelease"),
                    PMLOGKFV("X", "%.f", static_cast<WebOSMouseEvent*>(event)->GetX() * scale),
                    PMLOGKFV("Y", "%.f", static_cast<WebOSMouseEvent*>(event)->GetY() * scale), "");
            }
        }
        else if (event->GetType() == WebOSEvent::InputPanelVisible) {
            LOG_INFO(MSGID_VKB_EVENT, 4,
                PMLOGKS("APP_ID", m_webApp->appId().c_str()),
                PMLOGKS("INSTANCE_ID", m_webApp->instanceId().c_str()),
                PMLOGKS("STATUS", "InputPanelVisible"),
                PMLOGKS("Visible", static_cast<WebOSVirtualKeyboardEvent*>(event)->GetVisible() == true ? "true" : "false"), "");
        }
        else if (event->GetType() != WebOSEvent::MouseMove) {
            // log all window event except mouseMove
            // to print mouseMove event, set mouseMove : true
            LOG_INFO(MSGID_WINDOW_EVENT, 3,
                PMLOGKS("APP_ID", m_webApp->appId().c_str()),
                PMLOGKS("INSTANCE_ID", m_webApp->instanceId().c_str()),
                PMLOGKFV("TYPE", "%d", event->GetType()), "");
        }
    }
}

void WebAppWaylandWindow::sendKeyCode(int keyCode)
{
    if (!m_xinputActivated) {
        XInputActivate();
        m_xinputActivated = true;
    }
    XInputInvokeAction(keyCode);
}
