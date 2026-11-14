#pragma once

// Just enough COM to talk to WebView2 from MinGW: the SDK's own helpers need Microsoft's WRL, which MinGW does not have.

#include <windows.h>
#include <objbase.h>

#include <atomic>
#include <functional>
#include <utility>

#include "WebView2.h"

namespace launcher {

// The interface ids (MinGW's __uuidof needs them declared; the SDK header only has them as text). Generated into webview2_uuids.inc.
template <class T>
struct IidOf;
#define LAUNCHER_IID(type, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    template <>                                                       \
    struct IidOf<type> {                                              \
        static constexpr IID value = {l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}; \
    };
#include "webview2_uuids.inc"
#undef LAUNCHER_IID

// A smart pointer for COM objects.
template <class T>
class ComPtr {
public:
    ComPtr() = default;
    ComPtr(const ComPtr& other) : p_(other.p_) {
        if (p_ != nullptr) p_->AddRef();
    }
    ComPtr(ComPtr&& other) noexcept : p_(std::exchange(other.p_, nullptr)) {}
    ComPtr& operator=(ComPtr other) {
        std::swap(p_, other.p_);
        return *this;
    }
    ~ComPtr() { reset(); }

    void reset() {
        if (p_ != nullptr) p_->Release();
        p_ = nullptr;
    }
    // A pointer the callee has already AddRef'd for us (an out parameter).
    T** put() {
        reset();
        return &p_;
    }
    void attach(T* p) {
        reset();
        p_ = p;
    }
    // Takes a pointer and AddRefs it (an argument of a callback: it is only valid during the call).
    void set(T* p) {
        if (p != nullptr) p->AddRef();
        reset();
        p_ = p;
    }
    [[nodiscard]] T* get() const { return p_; }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }

    // Another interface of the same object (empty if it does not have it).
    template <class U>
    [[nodiscard]] ComPtr<U> as() const {
        ComPtr<U> out;
        if (p_ != nullptr) p_->QueryInterface(IidOf<U>::value, reinterpret_cast<void**>(out.put()));
        return out;
    }

private:
    T* p_ = nullptr;
};

// An object implementing a WebView2 "handler" interface (one method, Invoke) by calling a std::function.
template <class Handler, class... Args>
class Callback final : public Handler {
public:
    explicit Callback(std::function<HRESULT(Args...)> fn) : fn_(std::move(fn)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (out == nullptr) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IidOf<Handler>::value)) {
            *out = static_cast<Handler*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG n = --refs_;
        if (n == 0) delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE Invoke(Args... args) override { return fn_(args...); }

private:
    std::atomic<ULONG> refs_{1};
    std::function<HRESULT(Args...)> fn_;
};

// A handler to pass to a WebView2 call (which takes its own reference): a ComPtr owning the creation reference. The arguments are
// the parameters of the interface's Invoke, e.g.
//   makeHandler<ICoreWebView2NavigationCompletedEventHandler, ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*>(
//       [](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT { ... });
template <class Handler, class... Args, class F>
ComPtr<Handler> makeHandler(F&& fn) {
    ComPtr<Handler> out;
    out.attach(new Callback<Handler, Args...>(std::function<HRESULT(Args...)>(std::forward<F>(fn))));
    return out;
}

}  // namespace launcher
