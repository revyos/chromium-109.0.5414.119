// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-audio-and-captions-page' is the accessibility settings subpage for
 * audio and captions accessibility settings.
 */

import 'chrome://resources/cr_elements/cr_icon_button/cr_icon_button.js';
import 'chrome://resources/cr_elements/cr_link_row/cr_link_row.js';
import 'chrome://resources/cr_elements/cr_toggle/cr_toggle.js';
import 'chrome://resources/cr_elements/icons.html.js';
import 'chrome://resources/cr_elements/cr_shared_vars.css.js';
import '../../controls/settings_slider.js';
import '../../controls/settings_toggle_button.js';
import '../../settings_shared.css.js';
import '../../a11y_page/captions_subpage.js';
import 'chrome://resources/cr_components/localized_link/localized_link.js';

import {CrToggleElement} from 'chrome://resources/cr_elements/cr_toggle/cr_toggle.js';
import {I18nMixin, I18nMixinInterface} from 'chrome://resources/cr_elements/i18n_mixin.js';
import {WebUiListenerMixin, WebUiListenerMixinInterface} from 'chrome://resources/cr_elements/web_ui_listener_mixin.js';
import {loadTimeData} from 'chrome://resources/js/load_time_data.m.js';
import {mixinBehaviors, PolymerElement} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';

import {Setting} from '../../mojom-webui/setting.mojom-webui.js';
import {Route} from '../../router.js';
import {DeepLinkingBehavior, DeepLinkingBehaviorInterface} from '../deep_linking_behavior.js';
import {routes} from '../os_route.js';
import {RouteOriginBehavior, RouteOriginBehaviorImpl, RouteOriginBehaviorInterface} from '../route_origin_behavior.js';

import {getTemplate} from './audio_and_captions_page.html.js';
import {AudioAndCaptionsPageBrowserProxy, AudioAndCaptionsPageBrowserProxyImpl} from './audio_and_captions_page_browser_proxy.js';

interface SettingsAudioAndCaptionsPageElement {
  $: {
    startupSoundEnabled: CrToggleElement,
  };
}

const SettingsAudioAndCaptionsPageElementBase =
    mixinBehaviors(
        [
          DeepLinkingBehavior,
          RouteOriginBehavior,
        ],
        WebUiListenerMixin(I18nMixin(PolymerElement))) as {
      new (): PolymerElement & I18nMixinInterface &
          WebUiListenerMixinInterface & DeepLinkingBehaviorInterface &
          RouteOriginBehaviorInterface,
    };

class SettingsAudioAndCaptionsPageElement extends
    SettingsAudioAndCaptionsPageElementBase {
  static get is() {
    return 'settings-audio-and-captions-page';
  }

  static get template() {
    return getTemplate();
  }

  static get properties() {
    return {
      /**
       * Preferences state.
       */
      prefs: {
        type: Object,
        notify: true,
      },

      /**
       * Whether the user is in kiosk mode.
       */
      isKioskModeActive_: {
        type: Boolean,
        value() {
          return loadTimeData.getBoolean('isKioskModeActive');
        },
      },

      /**
       * Used by DeepLinkingBehavior to focus this page's deep links.
       */
      supportedSettingIds: {
        type: Object,
        value: () => new Set([
          Setting.kMonoAudio,
          Setting.kStartupSound,
        ]),
      },
    };
  }

  prefs: {[key: string]: any};
  private audioAndCaptionsBrowserProxy_: AudioAndCaptionsPageBrowserProxy;
  private isKioskModeActive_: boolean;
  private route_: Route;

  constructor() {
    super();

    /** RouteOriginBehavior override */
    this.route_ = routes.A11Y_AUDIO_AND_CAPTIONS;

    this.audioAndCaptionsBrowserProxy_ =
        AudioAndCaptionsPageBrowserProxyImpl.getInstance();
  }

  override ready() {
    super.ready();

    this.addWebUIListener(
        'initial-data-ready',
        (startupSoundEnabled: boolean) =>
            this.onAudioAndCaptionsPageReady_(startupSoundEnabled));
    this.audioAndCaptionsBrowserProxy_.audioAndCaptionsPageReady();
  }

  /**
   * Note: Overrides RouteOriginBehavior implementation
   */
  override currentRouteChanged(newRoute: Route, prevRoute?: Route) {
    RouteOriginBehaviorImpl.currentRouteChanged.call(this, newRoute, prevRoute);

    // Does not apply to this page.
    if (newRoute !== routes.A11Y_AUDIO_AND_CAPTIONS) {
      return;
    }

    this.attemptDeepLink();
  }

  private toggleStartupSoundEnabled_(e: CustomEvent<boolean>): void {
    this.audioAndCaptionsBrowserProxy_.setStartupSoundEnabled(e.detail);
  }

  /**
   * Handles updating the visibility of the shelf navigation buttons setting
   * and updating whether startupSoundEnabled is checked.
   */
  private onAudioAndCaptionsPageReady_(startupSoundEnabled: boolean): void {
    this.$.startupSoundEnabled.checked = startupSoundEnabled;
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-audio-and-captions-page': SettingsAudioAndCaptionsPageElement;
  }
}

customElements.define(
    SettingsAudioAndCaptionsPageElement.is,
    SettingsAudioAndCaptionsPageElement);
