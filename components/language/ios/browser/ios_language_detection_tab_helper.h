// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_LANGUAGE_IOS_BROWSER_IOS_LANGUAGE_DETECTION_TAB_HELPER_H_
#define COMPONENTS_LANGUAGE_IOS_BROWSER_IOS_LANGUAGE_DETECTION_TAB_HELPER_H_

#include "base/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "components/prefs/pref_member.h"
#import "ios/web/public/web_state_observer.h"
#import "ios/web/public/web_state_user_data.h"

namespace net {
class HttpResponseHeaders;
}

namespace translate {
struct LanguageDetectionDetails;
class LanguageDetectionModel;
}  // namespace translate

namespace language {

// Maximum length of the extracted text returned by |-extractTextContent|.
// Matches desktop implementation.
extern const size_t kMaxIndexChars;

class UrlLanguageHistogram;

// Dispatches language detection messages to language and translate components.
class IOSLanguageDetectionTabHelper
    : public web::WebStateObserver,
      public web::WebStateUserData<IOSLanguageDetectionTabHelper> {
 public:
  class Observer {
   public:
    // Called when language detection details become available.
    virtual void OnLanguageDetermined(
        const translate::LanguageDetectionDetails& details) = 0;
    // Called when the observed instance is being destroyed so that observers
    // can call RemoveObserver on the instance.
    virtual void IOSLanguageDetectionTabHelperWasDestroyed(
        IOSLanguageDetectionTabHelper* tab_helper) = 0;

    virtual ~Observer() {}
  };

  IOSLanguageDetectionTabHelper(const IOSLanguageDetectionTabHelper&) = delete;
  IOSLanguageDetectionTabHelper& operator=(
      const IOSLanguageDetectionTabHelper&) = delete;

  ~IOSLanguageDetectionTabHelper() override;

  // Adds or Removes observers.
  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Called on page language detection.
  void OnLanguageDetermined(const translate::LanguageDetectionDetails& details);

 private:
  friend class web::WebStateUserData<IOSLanguageDetectionTabHelper>;
  FRIEND_TEST_ALL_PREFIXES(IOSLanguageDetectionTabHelperTest,
                           TFLiteLanguageDetectionDurationRecorded);

  IOSLanguageDetectionTabHelper(
      web::WebState* web_state,
      UrlLanguageHistogram* url_language_histogram,
      translate::LanguageDetectionModel* language_detection_model,
      PrefService* prefs);

  // web::WebStateObserver implementation:
  void PageLoaded(
      web::WebState* web_state,
      web::PageLoadCompletionStatus load_completion_status) override;
  void DidFinishNavigation(web::WebState* web_state,
                           web::NavigationContext* navigation_context) override;
  void WebStateDestroyed(web::WebState* web_state) override;

  // Starts the page language detection and initiates the translation process.
  void StartLanguageDetection();

  // Handles the "languageDetection.textCaptured" javascript command.
  // |interacting| is true if the user is currently interacting with the page.
  void OnTextCaptured(const base::Value& value,
                      const GURL& url,
                      bool user_is_interacting,
                      web::WebFrame* sender_frame);

  // Completion handler used to retrieve the buffered text.
  void OnTextRetrieved(const bool has_notranslate,
                       const std::string& http_content_language,
                       const std::string& html_lang,
                       const GURL& url,
                       const base::Value* text_content);

  // Extracts "content-language" header into content_language_header_ variable.
  void ExtractContentLanguageHeader(net::HttpResponseHeaders* headers);

  // Selects and calls the correct DeterminePageLanguage based on the flags.
  std::string DeterminePageLanguage(const std::string& code,
                                    const std::string& html_lang,
                                    const std::u16string& contents,
                                    std::string* model_detected_language,
                                    bool* is_model_reliable,
                                    float& model_reliability_score,
                                    std::string* detection_model_version);

  // The WebState this instance is observing. Will be null after
  // WebStateDestroyed has been called.
  web::WebState* web_state_ = nullptr;
  UrlLanguageHistogram* const url_language_histogram_;
  translate::LanguageDetectionModel* language_detection_model_ = nullptr;
  // Subscription for JS message.
  base::CallbackListSubscription subscription_;
  BooleanPrefMember translate_enabled_;
  std::string content_language_header_;
  base::ObserverList<Observer, true>::Unchecked observer_list_;

  base::WeakPtrFactory<IOSLanguageDetectionTabHelper> weak_method_factory_;

  WEB_STATE_USER_DATA_KEY_DECL();
};

}  // namespace language

#endif  // COMPONENTS_LANGUAGE_IOS_BROWSER_IOS_LANGUAGE_DETECTION_TAB_HELPER_H_
