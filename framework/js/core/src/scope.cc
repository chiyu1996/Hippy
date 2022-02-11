/*
 *
 * Tencent is pleased to support the open source community by making
 * Hippy available.
 *
 * Copyright (C) 2019 THL A29 Limited, a Tencent company.
 * All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "core/scope.h"

#include <future>
#include <memory>
#include <string>
#include <vector>

#include "base/logging.h"
#include "core/base/common.h"
#include "core/engine.h"
#include "core/modules/module_register.h"
#include "core/napi/native_source_code.h"
#include "core/task/javascript_task.h"
#include "core/task/javascript_task_runner.h"
#ifdef JS_V8
#include "core/napi/v8/js_native_api_v8.h"
#endif

using unicode_string_view = tdf::base::unicode_string_view;

using RegisterMap = hippy::base::RegisterMap;
using RegisterFunction = hippy::base::RegisterFunction;
using ModuleClassMap = hippy::napi::ModuleClassMap;
using CtxValue = hippy::napi::CtxValue;
using TryCatch = hippy::napi::TryCatch;

constexpr char kDeallocFuncName[] = "HippyDealloc";
constexpr char kHippyBootstrapJSName[] = "bootstrap.js";
constexpr uint32_t kInvalidListenerId = 0;

Scope::Scope(Engine* engine,
             std::string name,
             std::unique_ptr<RegisterMap> map)
    : engine_(engine), context_(nullptr), name_(std::move(name)), map_(std::move(map)) {}

Scope::~Scope() {
  TDF_BASE_DLOG(INFO) << "~Scope";
  engine_->Exit();
}

void Scope::WillExit() {
  TDF_BASE_DLOG(INFO) << "WillExit begin";
  std::promise<std::shared_ptr<CtxValue>> promise;
  std::future<std::shared_ptr<CtxValue>> future = promise.get_future();
  std::weak_ptr<Ctx> weak_context = context_;
  JavaScriptTask::Function cb = hippy::base::MakeCopyable(
      [weak_context, p = std::move(promise)]() mutable {
        TDF_BASE_LOG(INFO) << "run js WillExit begin";
        std::shared_ptr<CtxValue> rst = nullptr;
        std::shared_ptr<Ctx> context = weak_context.lock();
        if (context) {
          std::shared_ptr<CtxValue> fn = context->GetJsFn(kDeallocFuncName);
          bool is_fn = context->IsFunction(fn);
          if (is_fn) {
            context->CallFunction(fn, 0, nullptr);
          }
        }
        p.set_value(rst);
      });
  std::shared_ptr<JavaScriptTaskRunner> runner = engine_->GetJSRunner();
  if (runner->IsJsThread()) {
    cb();
  } else {
    std::shared_ptr<JavaScriptTask> task = std::make_shared<JavaScriptTask>();
    task->callback = cb;
    runner->PostTask(task);
  }

  future.get();
  TDF_BASE_DLOG(INFO) << "ExitCtx end";
}

void Scope::Initialized() {
  TDF_BASE_DLOG(INFO) << "Scope Initialized";
  engine_->Enter();
  context_ = engine_->GetVM()->CreateContext();
  if (context_ == nullptr) {
    TDF_BASE_DLOG(ERROR) << "CreateContext return nullptr";
    return;
  }
  std::shared_ptr<Scope> self = wrapper_->scope_.lock();
  if (!self) {
    TDF_BASE_DLOG(ERROR) << "Scope wrapper_ error_";
    return;
  }
  RegisterMap::const_iterator it =
      map_->find(hippy::base::kContextCreatedCBKey);
  if (it != map_->end()) {
    RegisterFunction f = it->second;
    if (f) {
      TDF_BASE_DLOG(INFO) << "run ContextCreatedCB begin";
      f(wrapper_.get());
      TDF_BASE_DLOG(INFO) << "run ContextCreatedCB end";
      map_->erase(it);
    }
  }
  TDF_BASE_DLOG(INFO) << "Scope RegisterGlobalInJs";
  context_->RegisterGlobalModule(self,
                                 ModuleRegister::instance()->GetGlobalList());
  ModuleClassMap map(ModuleRegister::instance()->GetInternalList());
  binding_data_ = std::make_unique<BindingData>(self, map);

  auto source_code = hippy::GetNativeSourceCode(kHippyBootstrapJSName);
  TDF_BASE_DCHECK(source_code.data_ && source_code.length_);
  unicode_string_view str_view(source_code.data_, source_code.length_);
  std::shared_ptr<CtxValue> function = context_->RunScript(
      str_view, kHippyBootstrapJSName);

  bool is_func = context_->IsFunction(function);
  TDF_BASE_CHECK(is_func) << "bootstrap return not function, len = " << source_code.length_;
  // TODO(super): The following statement will be removed when TDF_BASE_CHECK will be cause abort
  if (!is_func) {
    return;
  }

  std::shared_ptr<CtxValue> internal_binding_fn =
      hippy::napi::GetInternalBindingFn(self);
  std::shared_ptr<CtxValue> argv[] = {internal_binding_fn};
  context_->CallFunction(function, 1, argv);

  it = map_->find(hippy::base::KScopeInitializedCBKey);
  if (it != map_->end()) {
    RegisterFunction f = it->second;
    if (f) {
      TDF_BASE_DLOG(INFO) << "run SCOPE_INITIALIZED begin";
      f(wrapper_.get());
      TDF_BASE_DLOG(INFO) << "run SCOPE_INITIALIZED end";
      map_->erase(it);
    }
  }
}

ModuleBase* Scope::GetModuleClass(const unicode_string_view& moduleName) {
  auto it = module_class_map_.find(moduleName);
  return it != module_class_map_.end() ? it->second.get() : nullptr;
}

void Scope::AddModuleClass(const unicode_string_view& name,
                           std::unique_ptr<ModuleBase> module) {
  module_class_map_.insert({name, std::move(module)});
}

std::shared_ptr<hippy::napi::CtxValue> Scope::GetModuleValue(
    const unicode_string_view& module_name) {
  auto it = module_value_map_.find(module_name);
  return it != module_value_map_.end() ? it->second : nullptr;
}

void Scope::AddModuleValue(const unicode_string_view& name,
                           const std::shared_ptr<CtxValue>& value) {
  module_value_map_.insert({name, value});
}

void Scope::SaveFunctionData(std::unique_ptr<hippy::napi::FunctionData> data) {
  function_data_.push_back(std::move(data));
}

void Scope::AddListener(uint32_t node_id, const std::string& event_name, uint32_t listener_id) {
  auto id_it = listener_id_map_.find(node_id);
  if (id_it != listener_id_map_.end()) {
    id_it->second[event_name] = listener_id; // 目前hippy上层还不支持绑定多个回调，update时替换原有回调
  } else {
    listener_id_map_[node_id] = std::unordered_map<std::string, uint32_t>();
    listener_id_map_[node_id][event_name] = listener_id;
  }
}

uint32_t Scope::GetListenerId(uint32_t node_id, const std::string& event_name) {
  auto id_it = listener_id_map_.find(node_id);
  if (id_it == listener_id_map_.end()) {
    return kInvalidListenerId;
  }
  auto name_it = id_it->second.find(event_name);
  if (name_it == id_it->second.end()) {
    return kInvalidListenerId;
  }
  return name_it->second;
}

void Scope::RunJS(const unicode_string_view& data,
                  const unicode_string_view& name,
                  bool is_copy) {
  std::weak_ptr<Ctx> weak_context = context_;
  JavaScriptTask::Function callback = [data, name, is_copy, weak_context] {
#ifdef JS_V8
    auto context = std::static_pointer_cast<hippy::napi::V8Ctx>(weak_context.lock());
    if (context) {
      context->RunScript(data, name, false, nullptr, is_copy);
    }
#else
    auto context = weak_context.lock();
    if (!context) {
      context->RunScript(data, name);
    }
#endif
  };

  std::shared_ptr<JavaScriptTaskRunner> runner = engine_->GetJSRunner();
  if (runner->IsJsThread()) {
    callback();
  } else {
    std::shared_ptr<JavaScriptTask> task = std::make_shared<JavaScriptTask>();
    task->callback = callback;
    runner->PostTask(task);
  }
}

std::shared_ptr<CtxValue> Scope::RunJSSync(const unicode_string_view& data,
                                           const unicode_string_view& name,
                                           bool is_copy) {
  std::promise<std::shared_ptr<CtxValue>> promise;
  std::future<std::shared_ptr<CtxValue>> future = promise.get_future();
  std::weak_ptr<Ctx> weak_context = context_;
  JavaScriptTask::Function cb = hippy::base::MakeCopyable(
      [data, name, is_copy, weak_context, p = std::move(promise)]() mutable {
        std::shared_ptr<CtxValue> rst = nullptr;
#ifdef JS_V8
        auto context = std::static_pointer_cast<hippy::napi::V8Ctx>(weak_context.lock());
        if (context) {
          rst = context->RunScript(data, name, false, nullptr, is_copy);
        }
#else
        auto context = weak_context.lock();
        if (context) {
          rst = context->RunScript(data, name);
        }
#endif
        p.set_value(rst);
      });

  std::shared_ptr<JavaScriptTaskRunner> runner = engine_->GetJSRunner();
  if (runner->IsJsThread()) {
    cb();
  } else {
    std::shared_ptr<JavaScriptTask> task = std::make_shared<JavaScriptTask>();
    task->callback = cb;
    runner->PostTask(task);
  }
  std::shared_ptr<CtxValue> ret = future.get();
  return ret;
}
