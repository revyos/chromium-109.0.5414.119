// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CORE_BROWSER_AUTOFILL_SUGGESTION_GENERATOR_H_
#define COMPONENTS_AUTOFILL_CORE_BROWSER_AUTOFILL_SUGGESTION_GENERATOR_H_

#include <map>
#include <string>
#include <vector>

#include "base/gtest_prod_util.h"
#include "base/memory/raw_ptr.h"
#include "base/types/id_type.h"
#include "components/autofill/core/browser/ui/suggestion.h"

namespace base {
class Time;
}  // namespace base

namespace autofill {

class AutofillClient;
class AutofillField;
class AutofillOfferData;
class AutofillType;
class CreditCard;
struct FormFieldData;
class FormStructure;
class IBAN;
class PersonalDataManager;

using InternalId = base::IdType32<class InternalIdTag>;

// Helper class to generate Autofill suggestions, such as for credit card and
// address profile Autofill.
class AutofillSuggestionGenerator {
 public:
  AutofillSuggestionGenerator(AutofillClient* autofill_client,
                              PersonalDataManager* personal_data);
  ~AutofillSuggestionGenerator();
  AutofillSuggestionGenerator(const AutofillSuggestionGenerator&) = delete;
  AutofillSuggestionGenerator& operator=(const AutofillSuggestionGenerator&) =
      delete;

  // Generates suggestions for all available profiles based on the `form`,
  // the value of `field` and the `autofill_field`. `app_locale` is the
  // locale used by the application.
  std::vector<Suggestion> GetSuggestionsForProfiles(
      const FormStructure& form,
      const FormFieldData& field,
      const AutofillField& autofill_field,
      const std::string& app_locale);

  // Generates suggestions for all available credit cards based on the `type`
  // and the value of `field`. `app_locale` is the locale used by the
  // application. `should_display_gpay_logo` will be set to true if there are no
  // credit card suggestions, or all suggestions come from Payments server.
  // `with_offer` is set to true if ANY card has card-linked offers.
  std::vector<Suggestion> GetSuggestionsForCreditCards(
      const FormFieldData& field,
      const AutofillType& type,
      const std::string& app_locale,
      bool* should_display_gpay_logo,
      bool* with_offer);

  // Generates suggestions for all available IBANs.
  static std::vector<Suggestion> GetSuggestionsForIBANs(
      const std::vector<IBAN*>& ibans);

  // Converts the vector of promo code offers that is passed in to a vector of
  // suggestions that can be displayed to the user for a promo code field.
  static std::vector<Suggestion> GetPromoCodeSuggestionsFromPromoCodeOffers(
      const std::vector<const AutofillOfferData*>& promo_code_offers);

  // Remove credit cards that are expired at |comparison_time| and not used
  // since |min_last_used| from |cards|. The relative ordering of |cards| is
  // maintained.
  static void RemoveExpiredCreditCardsNotUsedSinceTimestamp(
      base::Time comparison_time,
      base::Time min_last_used,
      std::vector<CreditCard*>* cards);

  // Return a nickname for the |card| to display. This is generally the nickname
  // stored in |card|, unless |card| exists as a local and a server copy. In
  // this case, we prefer the nickname of the local if it is defined. If only
  // one copy has a nickname, take that.
  std::u16string GetDisplayNicknameForCreditCard(const CreditCard& card) const;

  // Methods for packing and unpacking credit card and profile IDs for sending
  // and receiving to and from the renderer process.
  int MakeFrontendId(const Suggestion::BackendId& cc_backend_id,
                     const Suggestion::BackendId& profile_backend_id);
  void SplitFrontendId(int frontend_id,
                       Suggestion::BackendId* cc_backend_id,
                       Suggestion::BackendId* profile_backend_id);

  // Helper function to decide whether to show the virtual card option for
  // `candidate_card`.
  bool ShouldShowVirtualCardOption(const CreditCard* candidate_card) const;

  // Returns a pointer to the server card that has duplicate information of the
  // `local_card`. It is not guaranteed that a server card is found. If not,
  // nullptr is returned.
  const CreditCard* GetServerCardForLocalCard(
      const CreditCard* local_card) const;

  // Helper functions to expose functions to tests.
  InternalId BackendIdToInternalIdForTesting(
      const Suggestion::BackendId& backend_id);
  Suggestion::BackendId InternalIdToBackendIdForTesting(InternalId internal_id);

 protected:
  // Creates a suggestion for the given `credit_card`. `type` denotes the
  // AutofillType of the field that is focused when the query is triggered.
  // `prefix_matched_suggestion` indicates whether the suggestion has content
  // that prefix-matches the field content. `virtual_card_option` suggests
  // whether the suggestion is a virtual card option.
  // `card_linked_offer_available` indicates whether a card-linked offer is
  // attached to the `credit_card`.
  Suggestion CreateCreditCardSuggestion(const CreditCard& credit_card,
                                        const AutofillType& type,
                                        bool prefix_matched_suggestion,
                                        bool virtual_card_option,
                                        const std::string& app_locale,
                                        bool card_linked_offer_available) const;

  // Suggestion backend ID to internal ID mapping. We keep two maps to convert
  // back and forth. These should be used only by BackendIdToInternalId and
  // InternalIdToBackendId.
  // Note that the internal IDs are not frontend IDs.
  std::map<Suggestion::BackendId, InternalId> backend_to_internal_map_;
  std::map<InternalId, Suggestion::BackendId> internal_to_backend_map_;

 private:
  // Get the suggestion label for the `credit_card`. Note this does not account
  // for virtual cards or card-linked offers.
  std::u16string GetCardLabel(const CreditCard& credit_card,
                              const AutofillType& type,
                              const std::string& app_locale,
                              int obfuscation_length) const;

  // Adjust the content of |suggestion| if it is a virtual card suggestion.
  void AdjustVirtualCardSuggestionContent(Suggestion& suggestion,
                                          const CreditCard& credit_card,
                                          const AutofillType& type) const;

  // Set the URL for the card art image to be shown in the `suggestion`.
  void SetCardArtURL(Suggestion& suggestion,
                     const CreditCard& credit_card,
                     bool virtual_card_option) const;

  // Maps suggestion backend ID to and from an internal ID identifying it. Two
  // of these intermediate internal IDs are packed by MakeFrontendID to make the
  // IDs that this class generates for the UI and for IPC.
  InternalId BackendIdToInternalId(const Suggestion::BackendId& backend_id);
  Suggestion::BackendId InternalIdToBackendId(InternalId internal_id);

  // autofill_client_ and the generator are both one per tab, and have the same
  // lifecycle.
  raw_ptr<AutofillClient> autofill_client_;

  // personal_data_ should outlive the generator.
  raw_ptr<PersonalDataManager> personal_data_;
};

}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CORE_BROWSER_AUTOFILL_SUGGESTION_GENERATOR_H_
