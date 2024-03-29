#ifndef _CUTES_JS_UTIL_HPP_
#define _CUTES_JS_UTIL_HPP_
/*
 * Support for cutes on Qt5V8
 *
 * Copyright (C) 2013 Jolla Ltd.
 * Contact: Denis Zalevskiy <denis.zalevskiy@jolla.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.

 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html
 */

#include <QtQml/private/qv8engine_p.h>
#include <QtQml/private/qjsconverter_p.h>
#include <QtQml/private/qjsconverter_impl_p.h>

#include <QString>
#include <QVariant>

#include <functional>
#include <stdexcept>

namespace cutes {

QJSValue toQJSValue(QVariant const&);
QJSValue toQJSValue(QJSEngine &engine, QVariant const&);
QJSValue toQJSValue(QJSEngine &engine, v8::Handle<v8::Value>);
v8::Handle<v8::Value> fromQJSValue(QJSValue const&);

namespace js {

static inline QJSEngine *engine(v8::Arguments const &args)
{
    return QV8Engine::get(V8ENGINE());
}

static inline v8::Local<v8::String> toString(QString const &name)
{
    return v8::String::New(name.toUtf8().data());
}

static inline void V8ExceptionQString(QString const &msg)
{
    using namespace v8;
    ThrowException(Exception::Error(toString(msg)));
}

typedef v8::Handle<v8::Value> VHandle;

template <typename T>
struct ObjectTraits
{
};

static inline v8::Local<v8::Value> Get
(v8::Handle<v8::Object> v, char const *name)
{
    return v->Get(v8::String::New(name));
}

static inline void Set
(v8::Handle<v8::Object> o, char const *name, VHandle v)
{
    o->Set(v8::String::New(name), v);
}

template <typename T>
void v8DeleteCppObj(v8::Persistent<v8::Value> v, void* p)
{
    delete reinterpret_cast<T*>(p);
    v.Dispose();
    v.Clear();
}

static inline v8::Handle<v8::Function>
newFunction(QV8Engine *e, v8::InvocationCallback fn)
{
    using namespace v8;
    if (!e)
        throw std::logic_error("Null engine is passed");
    v8::HandleScope hscope;
    auto res = FunctionTemplate::New(fn, v8::External::New(e))->GetFunction();
    return hscope.Close(res);
}

static inline void Set
(QJSEngine &e, QJSValue v, QString const &name, v8::InvocationCallback fn)
{
    v8::HandleScope hscope;
    auto v8e = e.handle();
    v.setProperty(name, toQJSValue(e, newFunction(v8e, fn)));
}

VHandle callConvertException(const v8::Arguments &, v8::InvocationCallback);

template <typename T, typename ... Args>
auto callAnyConvertException
(T fn, Args&& ...args) -> decltype(fn(args...))
{
    try {
        return fn(args...);
    } catch (std::exception const &e) {
        using namespace v8;
        ThrowException(Exception::Error(String::New(e.what())));
        return decltype(fn(args...))();
    }
}

/**
 * @defgroup value_convert Converting Values
 *
 *  @{
 */

template <typename ResT>
ResT *cutesObjFromV8(v8::Handle<v8::Object> obj)
{
    auto cls = obj->Get(v8::String::New("cutesClass__"));
    if (cls.IsEmpty())
        throw std::invalid_argument("Not a cutes object");
    return reinterpret_cast<ResT*>
        (v8::External::Unwrap(obj->GetInternalField(0)));
}

template <typename T>
T * cutesObjFromThis(const v8::Arguments &args)
{
    return cutesObjFromV8<T>(args.This());
}

template <typename T>
static inline VHandle objToV8(T const& v)
{
    v8::HandleScope hscope;
    typedef typename ObjectTraits<T>::js_type obj_type;
    auto p = new T(v);
    VHandle external = v8::External::New(p);
    auto ctor = obj_type::cutesCtor_->GetFunction();
    auto obj = ctor->NewInstance(1, &external);
    auto ph = v8::Persistent<v8::Value>::New(obj);
    ph.MakeWeak(p, &v8DeleteCppObj<T>);
    obj->SetInternalField(0, external);

    return hscope.Close(obj);
}

template <typename T>
T * cutesObjFromV8(QV8Engine *, VHandle v)
{
    if (!v->IsObject())
        throw std::invalid_argument("Not an object");

    return cutesObjFromV8<T>(v8::Handle<v8::Object>::Cast(v));
}

template <typename T> struct Convert {};

template <typename T>
inline VHandle ValueToV8(T const& v)
{
    return Convert<T>::toV8(v);
};

template <typename T>
inline T ValueFromV8(QV8Engine *e, VHandle h)
{
    return Convert<T>::fromV8(e, h);
}

template<> struct Convert<QString> {
    static inline QString fromV8(QV8Engine *, VHandle v)
    {
        return QJSConverter::toString(v->ToString());
    }

    static inline VHandle toV8(QString const& v)
    {
        return QJSConverter::toString(v);
    }
};

char charFromV8(QV8Engine *, VHandle);

template<> struct Convert<char> {
    static inline char fromV8(QV8Engine *e, VHandle v)
    {
        return charFromV8(e, v);
    }

    static inline VHandle toV8(char v)
    {
        return QJSConverter::toString(QChar(v));
    }
};

template<> struct Convert<QDateTime> {
    static inline QDateTime fromV8(QV8Engine *, VHandle v)
    {
        using namespace v8;
        return QJSConverter::toDateTime(Handle<Date>::Cast(v));
    }

    static inline VHandle toV8(QDateTime const& v)
    {
        return QJSConverter::toDateTime(v);
    }
};

template<> struct Convert<int> {
    static inline int fromV8(QV8Engine *, VHandle v)
    {
        using namespace v8;
        if (!v->IsNumber()) {
            v8::String::Utf8Value cs(v);
            throw std::invalid_argument(std::string("Not a number: ") + *cs);
            return 0;
        }
        return v->ToInteger()->Value();
    }

    static inline VHandle toV8(int v)
    {
        return v8::Integer::New(v);
    }
};

template<> struct Convert<unsigned> {
    static inline int fromV8(QV8Engine *, VHandle v)
    {
        using namespace v8;
        if (!v->IsNumber()) {
            v8::String::Utf8Value cs(v);
            throw std::invalid_argument(std::string("Not a number: ") + *cs);
            return 0;
        }
        return v->ToInteger()->Value();
    }

    static inline VHandle toV8(unsigned v)
    {
        return v8::Integer::New(v);
    }
};

template<> struct Convert<long long> {
    static inline VHandle toV8(long long v)
    {
        return v8::Number::New(v);
    }
};

template<> struct Convert<bool> {
    static inline VHandle toV8(bool v)
    {
        return v8::Boolean::New(v);
    }
};

template<> struct Convert<QVariant> {
    static inline QVariant fromV8(QV8Engine *e, VHandle v)
    {
        return e->variantFromJS(v);
    }
};

QStringList QStringListFromV8(QV8Engine *, VHandle);
VHandle QStringListToV8(QStringList const &);

template<> struct Convert<QStringList> {
    static inline QStringList fromV8(QV8Engine *e, VHandle v)
    {
        return QStringListFromV8(e, v);
    }
    static inline VHandle toV8(QStringList const &src)
    {
        return QStringListToV8(src);
    }
};

template<typename T> struct Convert<QList<T> > {
    static QList<T> fromV8(QV8Engine *, VHandle v)
    {
        using namespace v8;
        QList<T> res;
        if (!v->IsArray()) {
            throw std::invalid_argument("Expecting array");
            return res;
        }
        auto self = Handle<Array>::Cast(v);
        for (int i = 0; i < self->Length(); ++i)
            res.push_back(ValueFromV8<T>(self->Get(i)));

        return res;
    }

    static VHandle toV8(QList<T> const &src)
    {
        using namespace v8;
        auto res = Array::New(src.length());
        int i = 0;
        for (auto const &v : src)
            res->Set(i++, ValueToV8<T>(v));
        return res;
    }
};

template <typename T>
T Arg(v8::Arguments const& args, unsigned i)
{
    return ValueFromV8<T>(V8ENGINE(), args[i]);
}

template <typename T>
T CtorArg(v8::Arguments const& args, unsigned i)
{
    try {
        return Arg<T>(args, i);
    } catch (std::exception const &e) {
        using namespace v8;
        auto msg = QString("Error constructing object: %1").arg(e.what());
        qWarning() << msg;
        V8ExceptionQString(msg);
        return T();
    }
}

/** @} */

/**
 * @defgroup class_add Adding Classes
 *
 *  @{
 */


std::pair<bool, VHandle> copyCtor(const v8::Arguments &args);

template <typename T, typename BaseT>
static VHandle v8Ctor(const v8::Arguments &args)
{
    auto res = copyCtor(args);
    if (res.first)
        return res.second;

    using namespace v8;
    auto self = args.This();
    T *p = new T(args);
    auto ph = Persistent<Value>::New(self);
    ph.MakeWeak(p, &v8DeleteCppObj<T>);
    self->SetInternalField(0, External::New(static_cast<BaseT*>(p)));
    return self;
}

template <typename T>
static void v8EngineAdd_(QV8Engine *v8e, v8::Local<v8::Object> tgt, char const *name)
{
    using namespace v8;
    HandleScope hscope;

    Handle<FunctionTemplate> ctor
        = FunctionTemplate::New(v8Ctor<T, typename T::base_type>
                                , External::New(v8e));

    T::cutesCtor_ = Persistent<FunctionTemplate>::New(ctor);

    Handle<ObjectTemplate> tpl = ctor->InstanceTemplate();
    tpl->SetInternalFieldCount(1);

    tpl->Set("cutesClass__", v8::String::New(name));

    T::v8Setup(v8e, ctor, tpl);
    tgt->Set(v8::String::New(name), ctor->GetFunction());
}

struct V8AddTag_ {};
template <typename T> struct V8Class_ {};

typedef std::tuple<V8AddTag_, QV8Engine *, v8::Local<v8::Object> > V8Add_;

static inline V8Add_ v8EngineAdd(QV8Engine *v8e, v8::Local<v8::Object> tgt)
{
    return std::make_tuple(V8AddTag_(), v8e, tgt);
}

template <typename T>
static inline std::tuple<V8Class_<T>, char const*> v8Class(char const *name)
{
    return std::tuple<V8Class_<T>, char const*>(V8Class_<T>(), name);
}

template <typename T>
V8Add_ const& operator <<
(V8Add_ const& helper, std::tuple<V8Class_<T>, char const*> const &data)
{
    v8EngineAdd_<T>(std::get<1>(helper), std::get<2>(helper), std::get<1>(data));
    return helper;
}

/** @} */

/**
 * @defgroup template_setup Setup Object Template
 *
 *  @{
 */

struct V8TemplateTag_ {};
typedef std::tuple<V8TemplateTag_, QV8Engine *
                   , v8::Handle<v8::Template> > V8Template_;

/**
 * returns object used as a target to streaming operation to add
 * members to the class/object template
 *
 * @param v8e v8 engine wrapper
 * @param tgt target template
 *
 * @return object to be used for << operations
 */
static inline V8Template_ setupTemplate(QV8Engine *v8e, v8::Handle<v8::Template> tgt)
{
    return std::make_tuple(V8TemplateTag_(), v8e, tgt);
}

template <typename T> struct V8Fn_ {};
template <typename T> struct V8Const_ {};

/**
 * info about v8 callback to be added
 *
 * @param name function name
 * @param fn implementation
 *
 * @return info to be passed to setupTemplate result
 */
template <typename T>
static inline std::tuple<V8Fn_<T>, char const*, T>
Callback(char const *name, T fn)
{
    return std::make_tuple(V8Fn_<T>(), name, fn);
}

/**
 *
 *
 * @param name function name
 * @param v value convertible to v8 value
 *
 * @return info to be passed to setupTemplate result
 */
template <typename T>
static inline std::tuple<V8Const_<T>, char const*, T>
Const(char const *name, T v)
{
    return std::make_tuple(V8Const_<T>(), name, v);
}

template <typename T>
V8Template_ const& operator <<
(V8Template_ const& helper, std::tuple<V8Fn_<T>, char const*, T> const &data)
{
    auto engine = std::get<1>(helper);
    auto &target = std::get<2>(helper);
    auto name = std::get<1>(data);
    auto &fn = std::get<2>(data);
    target->Set(name, V8FUNCTION(fn, engine));
    return helper;
}

template <typename T>
V8Template_ const& operator <<
(V8Template_ const& helper, std::tuple<V8Const_<T>, char const*, T> const &data)
{
    auto &target = std::get<2>(helper);
    auto name = std::get<1>(data);
    target->Set(name, ValueToV8(std::get<2>(data)));
    return helper;
}

/** @} */

template <typename ResT, typename ObjT>
struct FnWrapper
{
    template <typename FnT, FnT fn>
    static VHandle param0(const v8::Arguments &args)
    {
        return callConvertException(args, [](const v8::Arguments &args) -> VHandle {
                auto self = cutesObjFromThis<ObjT>(args);
                return ValueToV8((self->*fn)());
            });
    }

    template <typename ParamT, typename FnT, FnT fn>
    static VHandle param1(const v8::Arguments &args)
    {
        return callConvertException(args, [](const v8::Arguments &args) -> VHandle {
                auto self = cutesObjFromThis<ObjT>(args);
                if (args.Length() < 1)
                    throw std::invalid_argument("Need 2 parameters");
                auto p0 = Arg<ParamT>(args, 0);
                return ValueToV8((self->*fn)(p0));
            });
    }

    template <typename FnT, FnT fn, typename P1T, typename P2T>
    static VHandle param2(const v8::Arguments &args)
    {
        return callConvertException(args, [](const v8::Arguments &args) -> VHandle {
                auto self = cutesObjFromThis<ObjT>(args);
                if (args.Length() < 2)
                    throw std::invalid_argument("Need 2 parameters");
                auto p0 = Arg<P1T>(args, 0);
                auto p1 = Arg<P2T>(args, 1);
                return ValueToV8((self->*fn)(p0, p1));
            });
    }

};

template <typename ObjT>
struct FnWrapper<void, ObjT>
{
    template <typename FnT, FnT fn>
    static inline VHandle param0(const v8::Arguments &args)
    {
        return callConvertException(args, [](const v8::Arguments &args) -> VHandle {
                auto self = cutesObjFromThis<ObjT>(args);
                (self->*fn)();
                return v8::Undefined();
            });
    }

    template <typename ParamT, typename FnT, FnT fn>
    static inline VHandle param1(const v8::Arguments &args)
    {
        return callConvertException(args, [](const v8::Arguments &args) -> VHandle {
                auto self = cutesObjFromThis<ObjT>(args);
                auto p0 = Arg<ParamT>(args, 0);
                (self->*fn)(p0);
                return v8::Undefined();
            });
    }
};

template <typename ResT, typename ObjT, typename ParamT, typename FnT, FnT fn>
inline VHandle fnWithParam(const v8::Arguments &args)
{
    return FnWrapper<ResT, ObjT>::template param1<ParamT, FnT, fn>(args);
}

template <typename ResT, typename ObjT, typename FnT, FnT fn
          , typename P1T, typename P2T>
inline VHandle fnWithParam(const v8::Arguments &args)
{
    return FnWrapper<ResT, ObjT>::template param2<FnT, fn, P1T, P2T>(args);
}

template <typename ResT, typename ObjT, typename FnT, FnT fn>
static VHandle fnWOParams(const v8::Arguments &args)
{
    return FnWrapper<ResT, ObjT>::template param0<FnT, fn>(args);
}

#define CUTES_CONST(name, cls) cutes::js::Const(#name, cls::name)

#define CUTES_FN(name, cls) cutes::js::Callback(#name, &cls::name)

#define CUTES_GET_CONST(name, res, type, obj_type)   \
    cutes::js::Callback(#name, fnWOParams<res, type,                 \
                        res(obj_type::*)() const, &obj_type::name>)

#define CUTES_GET(name, res, type, obj_type)  \
    cutes::js::Callback(#name, fnWOParams                                   \
                            <res, type, res(obj_type::*)(), &obj_type::name>)

#define CUTES_VOID_FN(name, type, obj_type)                                      \
    cutes::js::Callback(#name, fnWOParams<void, type,                               \
                                          void(obj_type::*)(), &obj_type::name>)

#define CUTES_VOID_CONST_FN(name, type, obj_type)                                   \
    cutes::js::Callback(#name, fnWOParams<void, type,                                   \
                        void(obj_type::*)() const, &obj_type::name>)

#define CUTES_FN_PARAM(name, res, type, obj_type, param_t, param_sig)    \
    cutes::js::Callback(#name, fnWithParam                                  \
                            <res, type, param_t,                            \
                             res (obj_type::*)(param_sig),                  \
                             &obj_type::name>)

#define CUTES_FN_PARAM_CONST(name, res, type, obj_type, param_t, param_sig)    \
    cutes::js::Callback(#name, fnWithParam                                     \
                            <res, type, param_t,                               \
                             res (obj_type::*)(param_sig) const,               \
                             &obj_type::name>)

#define CUTES_FN_PARAM2(name, res, type, obj_type, p1_t, p2_t, param_sig) \
    cutes::js::Callback(#name, fnWithParam                                  \
                        <res, type,                                         \
                        res (obj_type::*)(param_sig),                       \
                        &obj_type::name, p1_t, p2_t>)

#define CUTES_FLAG_CONVERTIBLE_INT(flag_type)               \
template<> struct Convert<flag_type> {                      \
    static inline flag_type fromV8(QV8Engine *e, VHandle v) \
    {                                                       \
        return (flag_type)ValueFromV8<int>(e, v);                       \
    }                                                                   \
    static inline VHandle toV8(flag_type const& v)                      \
    {                                                                   \
        return ValueToV8((int)v);                                       \
    }                                                                   \
};

#define CUTES_CONVERTIBLE_COPYABLE(obj_type, js_type_)              \
    template <> struct ObjectTraits<obj_type>                       \
    {                                                               \
        typedef js_type_ js_type;                                   \
    };                                                              \
                                                                    \
    template <> struct Convert<obj_type>                            \
    {                                                               \
        static inline VHandle toV8(obj_type const& v)               \
        {                                                           \
            return objToV8<obj_type>(v);                            \
        }                                                           \
                                                                    \
        static inline obj_type const& fromV8(QV8Engine *v8e, VHandle v) \
        {                                                               \
            return *cutesObjFromV8<obj_type>(v8e, v);                   \
        }                                                               \
    };

}

static inline char const *cutesRegisterName()
{
    return "cutesRegister";
}

typedef QJSValue (*cutesRegisterFnType)(QJSEngine *);
}

extern "C" QJSValue cutesRegister(QJSEngine *);

#endif // _CUTES_JS_UTIL_HPP_
