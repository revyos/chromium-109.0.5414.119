// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Polymer element handling errors when installing an eSIM
 * profile, such as requiring a confirmation code.
 */

import 'chrome://resources/cr_elements/cr_dialog/cr_dialog.js';
import 'chrome://resources/cr_elements/cr_input/cr_input.js';
import 'chrome://resources/polymer/v3_0/iron-flex-layout/iron-flex-layout-classes.js';

import {I18nBehavior, I18nBehaviorInterface} from 'chrome://resources/ash/common/i18n_behavior.js';
import {ESimOperationResult, ESimProfileRemote, ProfileInstallResult} from 'chrome://resources/mojo/chromeos/ash/services/cellular_setup/public/mojom/esim_manager.mojom-webui.js';
import {html, mixinBehaviors, PolymerElement} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';

/**
 * @constructor
 * @extends {PolymerElement}
 * @implements {I18nBehaviorInterface}
 */
const EsimInstallErrorDialogElementBase =
    mixinBehaviors([I18nBehavior], PolymerElement);

/** @polymer */
class EsimInstallErrorDialogElement extends EsimInstallErrorDialogElementBase {
  static get is() {
    return 'esim-install-error-dialog';
  }

  static get template() {
    return html`{__html_template__}`;
  }

  static get properties() {
    return {
      /**
       * The error code returned when profile install attempt was made in
       * networks list.
       * @type {?ProfileInstallResult}
       */
      errorCode: {
        type: Object,
        value: null,
      },

      /** @type {?ESimProfileRemote} */
      profile: {
        type: Object,
        value: null,
      },

      /** @private {string} */
      confirmationCode_: {
        type: String,
        value: '',
        observer: 'onConfirmationCodeChanged_',
      },

      /** @private {boolean} */
      isInstallInProgress_: {
        type: Boolean,
        value: false,
      },

      /** @private {boolean} */
      isConfirmationCodeInvalid_: {
        type: Boolean,
        value: false,
      },
    };
  }

  /** @private */
  onConfirmationCodeChanged_() {
    this.isConfirmationCodeInvalid_ = false;
  }

  /**
   * @param {Event} event
   * @private
   */
  onDoneClicked_(event) {
    if (!this.isConfirmationCodeError_()) {
      this.$.installErrorDialog.close();
      return;
    }
    this.isInstallInProgress_ = true;
    this.isConfirmationCodeInvalid_ = false;

    this.profile.installProfile(this.confirmationCode_).then((response) => {
      this.isInstallInProgress_ = false;
      if (response.result === ESimOperationResult.kSuccess) {
        this.$.installErrorDialog.close();
        return;
      }
      // TODO(crbug.com/1093185) Only display confirmation code entry if the
      // error was an invalid confirmation code, else display generic error.
      this.isConfirmationCodeInvalid_ = true;
    });
  }

  /**
   * @param {Event} event
   * @private
   */
  onCancelClicked_(event) {
    this.$.installErrorDialog.close();
  }

  /**
   * @return {boolean}
   * @private
   */
  /** @private */
  isConfirmationCodeError_() {
    return this.errorCode === ProfileInstallResult.kErrorNeedsConfirmationCode;
  }

  /**
   * @return {boolean}
   * @private
   */
  isDoneButtonDisabled_() {
    return this.isConfirmationCodeError_() &&
        (!this.confirmationCode_ || this.isInstallInProgress_);
  }
}

customElements.define(
    EsimInstallErrorDialogElement.is, EsimInstallErrorDialogElement);
