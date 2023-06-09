// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/loader/speculation_rule_loader.h"

#include "services/network/public/cpp/header_util.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/inspector/console_message.h"
#include "third_party/blink/renderer/core/loader/resource/speculation_rules_resource.h"
#include "third_party/blink/renderer/core/speculation_rules/document_speculation_rules.h"
#include "third_party/blink/renderer/core/speculation_rules/speculation_rule_set.h"

namespace blink {

SpeculationRuleLoader::SpeculationRuleLoader(Document& document)
    : document_(document) {}

SpeculationRuleLoader::~SpeculationRuleLoader() = default;

void SpeculationRuleLoader::LoadResource(SpeculationRulesResource* resource,
                                         const KURL& base_url) {
  DCHECK(!resource_);
  base_url_ = base_url;
  resource_ = resource;
  resource_->AddFinishObserver(
      this, document_->GetTaskRunner(TaskType::kNetworking).get());
  DocumentSpeculationRules::From(*document_).AddSpeculationRuleLoader(this);
}

void SpeculationRuleLoader::NotifyFinished() {
  DCHECK(resource_);
  int response_code = resource_->GetResponse().HttpStatusCode();
  if (!network::IsSuccessfulStatus(response_code)) {
    document_->AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
        mojom::blink::ConsoleMessageSource::kOther,
        mojom::blink::ConsoleMessageLevel::kWarning,
        "Received a response with unsuccessful status code (" +
            String::Number(response_code) + ") for rule set requested from \"" +
            resource_->GetResourceRequest().Url().ElidedString() +
            "\" found in Speculation-Rules header."));
    return;
  }

  if (!EqualIgnoringASCIICase(resource_->HttpContentType(),
                              "application/speculationrules+json")) {
    document_->AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
        mojom::blink::ConsoleMessageSource::kOther,
        mojom::blink::ConsoleMessageLevel::kWarning,
        "Received a response with invalid MIME type \"" +
            resource_->HttpContentType() +
            "\" for the rule set requested from \"" +
            resource_->GetResourceRequest().Url().ElidedString() +
            "\" found in the Speculation-Rules header."));
    return;
  }
  if (!resource_->HasData()) {
    document_->AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
        mojom::blink::ConsoleMessageSource::kOther,
        mojom::blink::ConsoleMessageLevel::kWarning,
        "Received a response with no data for rule set \"" +
            resource_->GetResourceRequest().Url().ElidedString() +
            "\" found in Speculation-Rules "
            "header."));
    return;
  }

  const auto& source_text = resource_->DecodedText();
  String parse_error;
  if (auto* rule_set = SpeculationRuleSet::Parse(
          source_text, base_url_, document_->GetExecutionContext(),
          &parse_error)) {
    DocumentSpeculationRules::From(*document_).AddRuleSet(rule_set);
  }
  if (!parse_error.IsNull()) {
    document_->AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
        mojom::blink::ConsoleMessageSource::kOther,
        mojom::blink::ConsoleMessageLevel::kWarning,
        "While parsing speculation rules fetched from \"" +
            resource_->GetResourceRequest().Url().ElidedString() +
            "\": " + parse_error + "\"."));
  }
  resource_->RemoveFinishObserver(this);
  resource_ = nullptr;
  DocumentSpeculationRules::From(*document_).RemoveSpeculationRuleLoader(this);
}

void SpeculationRuleLoader::Trace(Visitor* visitor) const {
  visitor->Trace(document_);
  visitor->Trace(resource_);
  ResourceFinishObserver::Trace(visitor);
}

}  // namespace blink
