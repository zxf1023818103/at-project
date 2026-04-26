#pragma once

#include "AbstractCallbackTask.h"

namespace at {

template<typename Callback, int Selected>
class _CallbackTask;

template<typename CallbackReturnType>
class _CallbackTask<CallbackReturnType, 0> : public AbstractCallbackTask {
public:
    using _Callback = function<shared_ptr<CallbackReturnType>(void)>;

private:
    const int m_selected = 0;

    const _Callback m_callback;

    shared_ptr<CallbackReturnType> m_result;

protected:
    virtual void routine() override {
        m_result = m_callback();
    }

public:
    explicit _CallbackTask(_Callback callback) : AbstractCallbackTask(), m_callback(callback) {}

    virtual ~_CallbackTask() override = default;

    shared_ptr<CallbackReturnType> result() const {
        waitForExit();
        return m_result;
    }

    virtual bool success() const override {
        waitForExit();
        return m_result != nullptr;
    }
};

template<typename CallbackVoidReturnType>
class _CallbackTask<CallbackVoidReturnType, 1> : public AbstractCallbackTask {
public:
    using _Callback = function<void(void)>;

private:
    const _Callback m_callback;

protected:
    virtual void routine() override {
        m_callback();
    }

public:
    explicit _CallbackTask(_Callback callback) : AbstractCallbackTask(), m_callback(callback) {}

    virtual ~_CallbackTask() override = default;

    void result() const {
        waitForExit();
    }

    virtual bool success() const override {
        waitForExit();
        return true;
    }
};

template<typename CallbackBoolReturnType>
class _CallbackTask<CallbackBoolReturnType, 2> : public AbstractCallbackTask {
public:
    using _Callback = function<bool(void)>;

private:
    const _Callback m_callback;

    bool m_success = false;

protected:
    virtual void routine() override {
        m_success = m_callback();
    }

public:
    explicit _CallbackTask(_Callback callback) : AbstractCallbackTask(), m_callback(callback) {}

    virtual ~_CallbackTask() override = default;

    bool result() const {
        waitForExit();
        return m_success;
    }

    virtual bool success() const override {
        return result();
    }
};

template<typename CallbackScalarReturnType>
class _CallbackTask<CallbackScalarReturnType, 3> : public AbstractCallbackTask {
public:
    using _Callback = function<CallbackScalarReturnType(void)>;

private:
    const _Callback m_callback;

    CallbackScalarReturnType m_result;

protected:
    virtual void routine() override {
        m_result = m_callback();
    }

public:
    explicit _CallbackTask(_Callback callback) : AbstractCallbackTask(), m_callback(callback) {}

    virtual ~_CallbackTask() override = default;

    CallbackScalarReturnType result() const {
        waitForExit();
        return m_result;
    }

    virtual bool success() const override {
        waitForExit();
        return true;
    }
};

template<typename CallbackSmartPointerReturnType>
class _CallbackTask<CallbackSmartPointerReturnType, 4> : public AbstractCallbackTask {
public:
    using _Callback = function<CallbackSmartPointerReturnType(void)>;

private:
    const _Callback m_callback;

    CallbackSmartPointerReturnType m_result;

protected:
    virtual void routine() override {
        m_result = m_callback();
    }

public:
    explicit _CallbackTask(_Callback callback) : AbstractCallbackTask(), m_callback(callback) {}

    virtual ~_CallbackTask() override = default;

    CallbackSmartPointerReturnType result() const {
        waitForExit();
        return m_result;
    }

    virtual bool success() const override {
        waitForExit();
        return m_result != nullptr;
    }
};

template<typename CallbackReturnType>
using CallbackTask = _CallbackTask<CallbackReturnType,
    is_void_v<CallbackReturnType> ? 1 :
    is_same_v<CallbackReturnType, bool> ? 2 :
    is_scalar_v<CallbackReturnType> ? 3 :
    is_smart_ptr_v<CallbackReturnType> ? 4 : 0>;

template<typename CallbackReturnType>
using Callback = typename CallbackTask<CallbackReturnType>::_Callback;

}  // namespace at
