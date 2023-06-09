// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PAYMENTS_CONTENT_SERVICE_WORKER_PAYMENT_APP_FACTORY_H_
#define COMPONENTS_PAYMENTS_CONTENT_SERVICE_WORKER_PAYMENT_APP_FACTORY_H_

#include <map>
#include <memory>

#include "components/payments/content/payment_app_factory.h"

namespace payments {

class ServiceWorkerPaymentAppCreator;

// Retrieves service worker payment apps.
class ServiceWorkerPaymentAppFactory : public PaymentAppFactory {
 public:
  ServiceWorkerPaymentAppFactory();

  ServiceWorkerPaymentAppFactory(const ServiceWorkerPaymentAppFactory&) =
      delete;
  ServiceWorkerPaymentAppFactory& operator=(
      const ServiceWorkerPaymentAppFactory&) = delete;

  ~ServiceWorkerPaymentAppFactory() override;

  // PaymentAppFactory:
  void Create(base::WeakPtr<Delegate> delegate) override;

  // Called by ServiceWorkerPaymentAppCreator to remove itself from the list of
  // |creators_|, which deletes the caller object.
  void DeleteCreator(ServiceWorkerPaymentAppCreator* creator_raw_pointer);

 private:
  std::map<ServiceWorkerPaymentAppCreator*,
           std::unique_ptr<ServiceWorkerPaymentAppCreator>>
      creators_;
};

}  // namespace payments

#endif  // COMPONENTS_PAYMENTS_CONTENT_SERVICE_WORKER_PAYMENT_APP_FACTORY_H_
