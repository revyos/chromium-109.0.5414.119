// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/file_system_access/storage_manager_file_system_access.h"

#include <utility>

#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/network/public/mojom/web_sandbox_flags.mojom-blink.h"
#include "third_party/blink/public/common/browser_interface_broker_proxy.h"
#include "third_party/blink/public/mojom/file_system_access/file_system_access_error.mojom-blink.h"
#include "third_party/blink/public/mojom/file_system_access/file_system_access_manager.mojom-blink.h"
#include "third_party/blink/public/platform/web_content_settings_client.h"
#include "third_party/blink/renderer/bindings/core/v8/script_promise_resolver.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_throw_dom_exception.h"
#include "third_party/blink/renderer/core/execution_context/security_context.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/workers/worker_global_scope.h"
#include "third_party/blink/renderer/modules/file_system_access/file_system_access_error.h"
#include "third_party/blink/renderer/modules/file_system_access/file_system_directory_handle.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/weborigin/security_origin.h"
#include "third_party/blink/renderer/platform/wtf/casting.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"

namespace blink {

namespace {
// The name to use for the root directory of a sandboxed file system.
constexpr const char kSandboxRootDirectoryName[] = "";

void GetDirectoryImpl(ScriptPromiseResolver* resolver, bool allow_access) {
  ExecutionContext* context = resolver->GetExecutionContext();
  if (!context || !resolver->GetScriptState()->ContextIsValid())
    return;

  if (!allow_access) {
    auto* const isolate = resolver->GetScriptState()->GetIsolate();
    ScriptState::Scope scope(resolver->GetScriptState());
    resolver->Reject(V8ThrowDOMException::CreateOrEmpty(
        isolate, DOMExceptionCode::kSecurityError,
        "Storage directory access is denied."));
    return;
  }

  mojo::Remote<mojom::blink::FileSystemAccessManager> manager;
  context->GetBrowserInterfaceBroker().GetInterface(
      manager.BindNewPipeAndPassReceiver());

  auto* raw_manager = manager.get();
  raw_manager->GetSandboxedFileSystem(WTF::BindOnce(
      [](ScriptPromiseResolver* resolver,
         mojo::Remote<mojom::blink::FileSystemAccessManager>,
         mojom::blink::FileSystemAccessErrorPtr result,
         mojo::PendingRemote<mojom::blink::FileSystemAccessDirectoryHandle>
             handle) {
        ExecutionContext* context = resolver->GetExecutionContext();
        if (!context)
          return;
        if (result->status != mojom::blink::FileSystemAccessStatus::kOk) {
          file_system_access_error::Reject(resolver, *result);
          return;
        }
        resolver->Resolve(MakeGarbageCollected<FileSystemDirectoryHandle>(
            context, kSandboxRootDirectoryName, std::move(handle)));
      },
      WrapPersistent(resolver), std::move(manager)));
}

}  // namespace

// static
ScriptPromise StorageManagerFileSystemAccess::getDirectory(
    ScriptState* script_state,
    const StorageManager& storage,
    ExceptionState& exception_state) {
  ExecutionContext* context = ExecutionContext::From(script_state);

  if (!context->GetSecurityOrigin()->CanAccessFileSystem()) {
    if (context->IsSandboxed(network::mojom::blink::WebSandboxFlags::kOrigin)) {
      exception_state.ThrowSecurityError(
          "Storage directory access is denied because the context is "
          "sandboxed and lacks the 'allow-same-origin' flag.");
      return ScriptPromise();
    } else {
      exception_state.ThrowSecurityError("Storage directory access is denied.");
      return ScriptPromise();
    }
  }

  SECURITY_DCHECK(context->IsWindow() || context->IsWorkerGlobalScope());
  WebContentSettingsClient* content_settings_client = nullptr;
  if (auto* window = DynamicTo<LocalDOMWindow>(context)) {
    LocalFrame* frame = window->GetFrame();
    if (!frame) {
      exception_state.ThrowSecurityError("Storage directory access is denied.");
      return ScriptPromise();
    }
    content_settings_client = frame->GetContentSettingsClient();
  } else {
    content_settings_client =
        To<WorkerGlobalScope>(context)->ContentSettingsClient();
  }

  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver>(script_state);
  ScriptPromise result = resolver->Promise();

  if (content_settings_client) {
    content_settings_client->AllowStorageAccess(
        WebContentSettingsClient::StorageType::kFileSystem,
        WTF::BindOnce(&GetDirectoryImpl, WrapPersistent(resolver)));
  } else {
    GetDirectoryImpl(resolver, true);
  }

  return result;
}

}  // namespace blink
