#ifndef UNIQUEFUNCTION_HPPP
#define UNIQUEFUNCTION_HPPP

#include <functional>
#include <type_traits>


namespace tclib
{

    namespace uniquefunction_details
    {
        static constexpr std::size_t s_TinyStorageMaxCapacity = 32;
        static constexpr std::size_t s_TinyStorageAlignment =
                std::alignment_of<
                typename std::aligned_storage<s_TinyStorageMaxCapacity>::type>::value;

        using TinyStorage = typename std::aligned_storage<s_TinyStorageMaxCapacity,
                                                            s_TinyStorageAlignment>::type;
        using BigStorage = void*;

        union Storage
        {
            BigStorage m_big;
            TinyStorage m_tiny;
        };

        template <typename T>
        struct Wrapper
        {
            using type = T;
        };

        using SmallTag = std::true_type;
        using BigTag = std::false_type;

        template <typename T>
        using IsSmall = std::bool_constant<sizeof (T) <= sizeof (Storage::m_tiny)>;

        template <typename R, typename... Arg>
        struct Vtable
        {
            using StorageTypePtr = void*;
            using InvokePtrType = R(*)(Storage&, Arg&&...);
            using MovePtrType = void(*)(Storage&, Storage&);
            using DestructPtrType = void(*)(Storage&);

            const InvokePtrType m_invokePtr;
            const MovePtrType m_movePtr;
            const DestructPtrType m_destructPtr;

            constexpr Vtable() noexcept
                : m_invokePtr{[](Storage&, Arg&&...) ->R { throw std::bad_function_call(); }}
                , m_movePtr{[](Storage&, Storage&) noexcept -> void {}}
                , m_destructPtr{[](Storage&) noexcept -> void {}}
            {}

            template <typename C>
            explicit Vtable(Wrapper<C>) noexcept
            : Vtable(Wrapper<C>{}, IsSmall<C>{})
            {}

            template <typename C>
            Vtable(Wrapper<C>, BigTag) noexcept
            : m_invokePtr{[](Storage& storagePtr, Arg&&... arg) noexcept(noexcept(std::declval<C>()(arg...))) -> R
                            {
                                return (*static_cast<C*>(storagePtr.m_big))(std::forward<Arg>(arg)...);
                            }
                       }
            , m_movePtr{[](Storage& dstPtr, Storage& srcPtr) ->void
                            {
                                dstPtr.m_big = new C{std::move(*static_cast<C*>(srcPtr.m_big))};
                            }
                       }
            , m_destructPtr{[](Storage& src) -> void
                            {
                                delete static_cast<C*>(src.m_big);
                            }
                       }
            {
            }

            template <typename C>
            Vtable(Wrapper<C>, SmallTag) noexcept
            : m_invokePtr{[](Storage& storage, Arg&&... arg) noexcept(noexcept(std::declval<C>()(arg...))) -> R
                            {
                                //ponters to unrelated types can be casted to each other through void*
                                void* const ptr = std::addressof(storage.m_tiny);
                                return (*static_cast<C*>(ptr))(std::forward<Arg>(arg)...);
                            }
                        }
            , m_movePtr{[](Storage& dst, Storage& src) noexcept(std::is_nothrow_move_constructible<C>::value) ->void
                            {
                                void* const srcPtr = std::addressof(src.m_tiny);
                                void* const dstPtr = std::addressof(dst.m_tiny);
                                new(dstPtr) C{std::move(*static_cast<C*>(srcPtr))};
                            }
                       }
            , m_destructPtr{[](Storage& storage) -> void
                        {
                            void* const ptr = std::addressof(storage.m_tiny);
                            static_cast<C*>(ptr)->~C();
                        }
                   }
                {
                }
        };


        //Note: variable template (actually is a global variable)
        template <typename R, typename... Arg>
        inline constexpr Vtable<R, Arg...> EmptyVtable{};

        template <typename F, typename R, typename... Arg>
        inline Vtable<R, Arg...> UniqueFunctionVtable{Wrapper<F>{}};
    }

    template <typename Signature>
    class UniqueFunction;

    template <typename R, typename... Arg>
    class UniqueFunction<R(Arg...)> final
    {
    public:
        UniqueFunction() noexcept
            : m_vtablePtr{std::addressof(uniquefunction_details::EmptyVtable<R, Arg...>)}
        {}

        //std::decay_t models the type conversions applied to function arguments when passed by value
        //std::is_same<C, UniqueFunction>::value is true for case "auto f2 = f;"
        //its's called instead of copy constructor for non-const objects
        template <typename F, typename C = std::decay_t<F>,
                  typename = typename std::enable_if_t<!(std::is_same<C, UniqueFunction>::value)>>

        UniqueFunction(F&& closure)
            : m_vtablePtr{std::addressof(uniquefunction_details::UniqueFunctionVtable<C, R, Arg...>)}
        {
            if constexpr (uniquefunction_details::IsSmall<C>{})
            {
                new(std::addressof(m_storage.m_tiny)) C{std::forward<F>(closure)};
            }
            else
            {
                m_storage.m_big = new C{std::forward<F>(closure)};
            }
        }

        //Without this constructor the code UniqueFunction f(nullptr); - does not compile
        constexpr UniqueFunction(std::nullptr_t)
            : m_vtablePtr{std::addressof(uniquefunction_details::EmptyVtable<R, Arg...>)} {}

        UniqueFunction& operator=(std::nullptr_t)
        {
            m_vtablePtr->m_destructPtr(m_storage);
            m_vtablePtr = std::addressof(uniquefunction_details::EmptyVtable<R, Arg...>);
            return *this;
        }

        UniqueFunction(const UniqueFunction&) = delete;
        UniqueFunction& operator=(const UniqueFunction&) = delete;

        UniqueFunction(UniqueFunction&& other)
            : m_vtablePtr{std::exchange(other.m_vtablePtr,
                                        std::addressof(uniquefunction_details::EmptyVtable<R, Arg...>))}
        {
            m_vtablePtr->m_movePtr(m_storage, other.m_storage);
        }

        UniqueFunction& operator=(UniqueFunction&& other)
        {
            if (this != std::addressof(other))
            {
                //Note: call destructor because we will create/inplace a new object and the existing one should be destructed.
                m_vtablePtr->m_destructPtr(m_storage);

                m_vtablePtr = std::exchange(other.m_vtablePtr,
                                            std::addressof(uniquefunction_details::EmptyVtable<R, Arg...>));
                m_vtablePtr->m_movePtr(m_storage, other.m_storage);
            }
            return *this;
        }

        R operator()(Arg... arg) const
        {
            return m_vtablePtr->m_invokePtr(m_storage, std::forward<Arg>(arg)...);
        }

        explicit operator bool() const noexcept
        {
            return (m_vtablePtr != std::addressof(uniquefunction_details::EmptyVtable<R, Arg...>));
        }

        bool operator==(std::nullptr_t) const noexcept
        {
            return !operator bool();
        }

        bool operator!=(std::nullptr_t) const noexcept
        {
            return operator bool();
        }

        void swap(UniqueFunction& other)
        {
            if (this == std::addressof(other))
            {
                return;
            }
            uniquefunction_details::Storage tmp;
            m_vtablePtr->m_movePtr(tmp, m_storage);
            other.m_vtablePtr->m_movePtr(m_storage, other.m_storage);
            m_vtablePtr->m_movePtr(other.m_storage, tmp);

            std::swap(m_vtablePtr, other.m_vtablePtr);
        }

        friend void swap(UniqueFunction& lhs, UniqueFunction& rhs)
        {
            lhs.swap(rhs);
        }

        ~UniqueFunction()
        {
            m_vtablePtr->m_destructPtr(m_storage);
        }

    private:
        using VtableType = uniquefunction_details::Vtable<R, Arg...>;

        const VtableType* m_vtablePtr;
        mutable uniquefunction_details::Storage m_storage;
    };

    template <typename R, typename... Arg>
    inline bool operator==(std::nullptr_t, const UniqueFunction<R(Arg...)>& f) noexcept
    {
        return (f == nullptr);
    }

    template <typename R, typename... Arg>
    inline bool operator!=(std::nullptr_t, const UniqueFunction<R(Arg...)>& f) noexcept
    {
        return (f != nullptr);
    }
}

#endif // UNIQUEFUNCTION_HPPP
