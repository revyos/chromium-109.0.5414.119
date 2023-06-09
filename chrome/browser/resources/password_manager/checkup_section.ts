// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import 'chrome://resources/cr_elements/cr_button/cr_button.js';
import 'chrome://resources/cr_elements/cr_link_row/cr_link_row.js';
import 'chrome://resources/cr_elements/cr_icon_button/cr_icon_button.js';
import 'chrome://resources/cr_elements/cr_icons.css.js';
import 'chrome://resources/cr_elements/cr_shared_style.css.js';
import 'chrome://resources/cr_elements/icons.html.js';
import 'chrome://resources/polymer/v3_0/paper-spinner/paper-spinner-lite.js';
import './shared_style.css.js';

import {CrButtonElement} from 'chrome://resources/cr_elements/cr_button/cr_button.js';
import {CrIconButtonElement} from 'chrome://resources/cr_elements/cr_icon_button/cr_icon_button.js';
import {CrLinkRowElement} from 'chrome://resources/cr_elements/cr_link_row/cr_link_row.js';
import {I18nMixin} from 'chrome://resources/cr_elements/i18n_mixin.js';
import {assert} from 'chrome://resources/js/assert_ts.js';
import {PluralStringProxyImpl} from 'chrome://resources/js/plural_string_proxy.js';
import {PaperSpinnerLiteElement} from 'chrome://resources/polymer/v3_0/paper-spinner/paper-spinner-lite.js';
import {PolymerElement} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';

import {getTemplate} from './checkup_section.html.js';
import {CredentialsChangedListener, PasswordCheckInteraction, PasswordCheckStatusChangedListener, PasswordManagerImpl} from './password_manager_proxy.js';

const CheckState = chrome.passwordsPrivate.PasswordCheckState;

export interface CheckupSectionElement {
  $: {
    checkupResult: HTMLAnchorElement,
    lastCheckupTime: HTMLAnchorElement,
    refreshButton: CrIconButtonElement,
    retryButton: CrButtonElement,
    spinner: PaperSpinnerLiteElement,
    compromisedRow: CrLinkRowElement,
    reusedRow: CrLinkRowElement,
    weakRow: CrLinkRowElement,
  };
}

export class CheckupSectionElement extends I18nMixin
(PolymerElement) {
  static get is() {
    return 'checkup-section';
  }

  static get template() {
    return getTemplate();
  }

  static get properties() {
    return {
      /**
       * The number of checked passwords as a formatted string.
       */
      checkedPasswordsText_: String,

      /**
       * The number of compromised passwords as a formatted string.
       */
      compromisedPasswordsText_: String,

      /**
       * The number of weak passwords as a formatted string.
       */
      reusedPasswordsText_: String,

      /**
       * The number of weak passwords as a formatted string.
       */
      weakPasswordsText_: String,

      /**
       * The status indicates progress and affects banner, title and icon.
       */
      status_: {
        type: Object,
        value: () => ({state: chrome.passwordsPrivate.PasswordCheckState.IDLE}),
      },

      isCheckRunning_: {
        type: Boolean,
        computed: 'computeIsCheckRunning_(status_)',
      },

      isCheckSuccessful_: {
        type: Boolean,
        computed: 'computeIsCheckSuccessful_(status_)',
      },
    };
  }

  private checkedPasswordsText_: string;
  private compromisedPasswordsText_: string;
  private reusedPasswordsText_: string;
  private weakPasswordsText_: string;
  private status_: chrome.passwordsPrivate.PasswordCheckStatus;
  private compromisedPasswords_: chrome.passwordsPrivate.PasswordUiEntry[] = [];
  private weakPasswords_: chrome.passwordsPrivate.PasswordUiEntry[] = [];
  private reusedPasswords_: chrome.passwordsPrivate.PasswordUiEntry[] = [];

  private statusChangedListener_: PasswordCheckStatusChangedListener|null =
      null;
  private insecureCredentialsChangedListener_: CredentialsChangedListener|null =
      null;

  override ready() {
    super.ready();
  }

  override connectedCallback() {
    super.connectedCallback();

    this.statusChangedListener_ = status => {
      this.status_ = status;
    };

    this.insecureCredentialsChangedListener_ = insecureCredentials => {
      this.compromisedPasswords_ = insecureCredentials.filter(cred => {
        return !cred.compromisedInfo!.isMuted &&
            cred.compromisedInfo!.compromiseTypes.some(type => {
              return (
                  type === chrome.passwordsPrivate.CompromiseType.LEAKED ||
                  type === chrome.passwordsPrivate.CompromiseType.PHISHED);
            });
      });

      this.reusedPasswords_ = insecureCredentials.filter(cred => {
        return cred.compromisedInfo!.compromiseTypes.some(type => {
          return type === chrome.passwordsPrivate.CompromiseType.REUSED;
        });
      });

      this.weakPasswords_ = insecureCredentials.filter(cred => {
        return cred.compromisedInfo!.compromiseTypes.some(type => {
          return type === chrome.passwordsPrivate.CompromiseType.WEAK;
        });
      });

      this.fetchPluralizedStrings_();
    };

    PasswordManagerImpl.getInstance().getPasswordCheckStatus().then(
        this.statusChangedListener_);
    PasswordManagerImpl.getInstance().addPasswordCheckStatusListener(
        this.statusChangedListener_);

    PasswordManagerImpl.getInstance().getInsecureCredentials().then(
        this.insecureCredentialsChangedListener_);
    PasswordManagerImpl.getInstance().addInsecureCredentialsListener(
        this.insecureCredentialsChangedListener_);
  }

  override disconnectedCallback() {
    super.disconnectedCallback();

    assert(this.statusChangedListener_);
    PasswordManagerImpl.getInstance().removePasswordCheckStatusListener(
        this.statusChangedListener_);
    this.statusChangedListener_ = null;

    assert(this.insecureCredentialsChangedListener_);
    PasswordManagerImpl.getInstance().removeInsecureCredentialsListener(
        this.insecureCredentialsChangedListener_);
    this.insecureCredentialsChangedListener_ = null;
  }

  private fetchPluralizedStrings_() {
    const proxy = PluralStringProxyImpl.getInstance();

    proxy.getPluralString('checkedPasswords', 6)
        .then(result => this.checkedPasswordsText_ = result);
    proxy
        .getPluralString(
            'compromisedPasswords', this.compromisedPasswords_.length)
        .then(result => this.compromisedPasswordsText_ = result);
    proxy.getPluralString('reusedPasswords', this.reusedPasswords_.length)
        .then(result => this.reusedPasswordsText_ = result);
    proxy.getPluralString('weakPasswords', this.weakPasswords_.length)
        .then(result => this.weakPasswordsText_ = result);
  }

  /**
   * @return true iff a check is running right according to the given |status|.
   */
  private computeIsCheckRunning_(): boolean {
    return this.status_.state === CheckState.RUNNING;
  }

  private computeIsCheckSuccessful_(): boolean {
    return this.status_.state === CheckState.IDLE;
  }

  private showRetryButton_(): boolean {
    return !this.computeIsCheckRunning_() && !this.computeIsCheckSuccessful_();
  }

  private showCheckButton_(): boolean {
    return this.status_.state !== CheckState.NO_PASSWORDS &&
        this.status_.state !== CheckState.QUOTA_LIMIT;
  }

  /**
   * Starts/Restarts bulk password check.
   */
  private onPasswordCheckButtonClick_() {
    PasswordManagerImpl.getInstance().startBulkPasswordCheck();
    PasswordManagerImpl.getInstance().recordPasswordCheckInteraction(
        PasswordCheckInteraction.START_CHECK_MANUALLY);
  }

  private getBannerImageFileName_(): string {
    if (this.computeIsCheckRunning_()) {
      return 'checkup_result_banner_running';
    }
    if (this.computeIsCheckSuccessful_()) {
      const hasIssues = !!this.compromisedPasswords_.length ||
          !!this.reusedPasswords_.length || !!this.weakPasswords_.length;
      return hasIssues ? 'checkup_result_banner_compromised' :
                         'checkup_result_banner_ok';
    }
    return 'checkup_result_banner_error';
  }

  private getIcon_(issues: chrome.passwordsPrivate.PasswordUiEntry[]): string {
    return issues.length ? 'cr:error' : 'cr:check-circle';
  }

  private hasIssues_(issues: chrome.passwordsPrivate.PasswordUiEntry[]):
      boolean {
    return !!issues.length;
  }

  private getCompromisedSectionSublabel_(): string {
    return this.compromisedPasswords_.length ?
        this.i18n('compromisedPasswordsTitle') :
        this.i18n('compromisedPasswordsEmpty');
  }

  private getReusedSectionSublabel_(): string {
    return this.reusedPasswords_.length ? this.i18n('reusedPasswordsTitle') :
                                          this.i18n('reusedPasswordsEmpty');
  }

  private getWeakSectionSublabel_(): string {
    return this.weakPasswords_.length ? this.i18n('weakPasswordsTitle') :
                                        this.i18n('weakPasswordsEmpty');
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'checkup-section': CheckupSectionElement;
  }
}

customElements.define(CheckupSectionElement.is, CheckupSectionElement);
