// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/workers/parent_execution_context_task_runners.h"

#include "base/synchronization/lock.h"
#include "third_party/blink/public/platform/task_type.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"

namespace blink {

ParentExecutionContextTaskRunners* ParentExecutionContextTaskRunners::Create(
    ExecutionContext& context) {
  return MakeGarbageCollected<ParentExecutionContextTaskRunners>(context);
}

ParentExecutionContextTaskRunners::ParentExecutionContextTaskRunners(
    ExecutionContext& context)
    : ExecutionContextLifecycleObserver(&context) {
  DCHECK(context.IsContextThread());
  InitializeTaskRunnersForContext(context);
}

void ParentExecutionContextTaskRunners::InitializeTaskRunnersForContext(
    ExecutionContext& context) {
  // For now we only support very limited task types. Sort in the TaskType enum
  // value order.
  for (auto type : {TaskType::kNetworking, TaskType::kPostedMessage,
                    TaskType::kWorkerAnimation, TaskType::kInternalDefault,
                    TaskType::kInternalLoading, TaskType::kInternalTest,
                    TaskType::kInternalMedia, TaskType::kInternalInspector}) {
    task_runners_.insert(type, context.GetTaskRunner(type));
  }
}

scoped_refptr<base::SingleThreadTaskRunner>
ParentExecutionContextTaskRunners::Get(TaskType type) {
  base::AutoLock locker(lock_);
  return task_runners_.at(type);
}

void ParentExecutionContextTaskRunners::Trace(Visitor* visitor) const {
  ExecutionContextLifecycleObserver::Trace(visitor);
}

void ParentExecutionContextTaskRunners::ContextDestroyed() {
  base::AutoLock locker(lock_);
  // When an ExecutionContext is destroyed we can still get task runners for it
  // but they might be changed.
  InitializeTaskRunnersForContext(*GetExecutionContext());
}

}  // namespace blink
