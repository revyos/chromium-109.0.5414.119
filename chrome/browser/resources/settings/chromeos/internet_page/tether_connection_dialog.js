// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'chrome://resources/ash/common/network/network_icon.js';
import 'chrome://resources/cr_elements/cr_button/cr_button.js';
import 'chrome://resources/cr_elements/cr_dialog/cr_dialog.js';
import 'chrome://resources/cr_elements/cr_shared_vars.css.js';
import 'chrome://resources/polymer/v3_0/iron-flex-layout/iron-flex-layout-classes.js';
import 'chrome://resources/polymer/v3_0/iron-icon/iron-icon.js';
import '../os_icons.js';
import '../../settings_shared.css.js';

import {OncMojo} from 'chrome://resources/ash/common/network/onc_mojo.js';
import {I18nBehavior, I18nBehaviorInterface} from 'chrome://resources/ash/common/i18n_behavior.js';
import {HTMLEscape} from 'chrome://resources/js/util.js';
import {ManagedProperties} from 'chrome://resources/mojo/chromeos/services/network_config/public/mojom/cros_network_config.mojom-webui.js';
import {html, mixinBehaviors, PolymerElement} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';

/**
 * Maps signal strength from [0, 100] to [0, 4] which represents the number
 * of bars in the signal icon displayed to the user. This is used to select
 * the correct icon.
 * @param {number} strength The signal strength from [0 - 100].
 * @return {number} The number of signal bars from [0, 4] as an integer
 */
function signalStrengthToBarCount(strength) {
  if (strength > 75) {
    return 4;
  }
  if (strength > 50) {
    return 3;
  }
  if (strength > 25) {
    return 2;
  }
  if (strength > 0) {
    return 1;
  }
  return 0;
}

/**
 * @constructor
 * @extends {PolymerElement}
 * @implements {I18nBehaviorInterface}
 */
const TetherConnectionDialogElementBase =
    mixinBehaviors([I18nBehavior], PolymerElement);

/** @polymer */
export class TetherConnectionDialogElement extends
    TetherConnectionDialogElementBase {
  static get is() {
    return 'tether-connection-dialog';
  }

  static get template() {
    return html`{__html_template__}`;
  }

  static get properties() {
    return {
      /** @type {!ManagedProperties|undefined} */
      managedProperties: Object,

      /**
       * Whether the network has been lost (e.g., has gone out of range).
       * @type {boolean}
       */
      outOfRange: Boolean,

    };
  }

  open() {
    const dialog = this.getDialog_();
    if (!dialog.open) {
      this.getDialog_().showModal();
    }

    this.$.connectButton.focus();
  }

  close() {
    const dialog = this.getDialog_();
    if (dialog.open) {
      dialog.close();
    }
  }

  /**
   * @return {!CrDialogElement}
   * @private
   */
  getDialog_() {
    return /** @type {!CrDialogElement} */ (this.$.dialog);
  }

  /** @private */
  onNotNowTap_() {
    this.getDialog_().cancel();
  }

  /**
   * Fires the 'connect-tap' event.
   * @private
   */
  onConnectTap_() {
    const event =
        new CustomEvent('tether-connect', {bubbles: true, composed: true});
    this.dispatchEvent(event);
  }

  /**
   * @param {!ManagedProperties|undefined} managedProperties
   * @return {boolean}
   * @private
   */
  shouldShowDisconnectFromWifi_(managedProperties) {
    // TODO(khorimoto): Pipe through a new network property which describes
    // whether the tether host is currently connected to a Wi-Fi network. Return
    // whether it is here.
    return true;
  }

  /**
   * @param {!ManagedProperties|undefined} managedProperties
   * @return {string} The battery percentage integer value converted to a
   *     string. Note that this will not return a string with a "%" suffix.
   * @private
   */
  getBatteryPercentageAsString_(managedProperties) {
    return managedProperties ?
        managedProperties.typeProperties.tether.batteryPercentage.toString() :
        '0';
  }

  /**
   * Retrieves an image that corresponds to signal strength of the tether host.
   * Custom icons are used here instead of a <network-icon> because this
   * dialog uses a special color scheme.
   * @param {!ManagedProperties|undefined}
   *    managedProperties
   * @return {string} The name of the icon to be used to represent the network's
   *     signal strength.
   */
  getSignalStrengthIconName_(managedProperties) {
    const signalStrength = managedProperties ?
        managedProperties.typeProperties.tether.signalStrength :
        0;
    return 'os-settings:signal-cellular-' +
        signalStrengthToBarCount(signalStrength) + '-bar';
  }

  /**
   * Retrieves a localized accessibility label for the signal strength.
   * @param {!ManagedProperties|undefined} managedProperties
   * @return {string} The localized signal strength label.
   */
  getSignalStrengthLabel_(managedProperties) {
    const signalStrength = managedProperties ?
        managedProperties.typeProperties.tether.signalStrength :
        0;
    const networkTypeString = this.i18n('OncTypeTether');
    return this.i18n(
        'networkIconLabelSignalStrength', networkTypeString, signalStrength);
  }

  /**
   * @param {!ManagedProperties|undefined} managedProperties
   * @return {string}
   * @private
   */
  getDeviceName_(managedProperties) {
    return managedProperties ? OncMojo.getNetworkName(managedProperties) : '';
  }

  /**
   * @param {!ManagedProperties|undefined} managedProperties
   * @return {string}
   * @private
   */
  getBatteryPercentageString_(managedProperties) {
    return managedProperties ?
        this.i18n(
            'tetherConnectionBatteryPercentage',
            this.getBatteryPercentageAsString_(managedProperties)) :
        '';
  }

  /**
   * @param {!ManagedProperties|undefined} managedProperties
   * @return {string}
   * @private
   */
  getExplanation_(managedProperties) {
    return managedProperties ?
        this.i18n(
            'tetherConnectionExplanation',
            HTMLEscape(OncMojo.getNetworkName(managedProperties))) :
        '';
  }

  /**
   * @param {!ManagedProperties|undefined} managedProperties
   * @return {string}
   * @private
   */
  getDescriptionTitle_(managedProperties) {
    return managedProperties ?
        this.i18n(
            'tetherConnectionDescriptionTitle',
            HTMLEscape(OncMojo.getNetworkName(managedProperties))) :
        '';
  }

  /**
   * @param {!ManagedProperties|undefined} managedProperties
   * @return {string}
   * @private
   */
  getBatteryDescription_(managedProperties) {
    return managedProperties ?
        this.i18n(
            'tetherConnectionDescriptionBattery',
            this.getBatteryPercentageAsString_(managedProperties)) :
        '';
  }
}

customElements.define(
    TetherConnectionDialogElement.is, TetherConnectionDialogElement);
