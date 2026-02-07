#pragma once
#include <cstdint>

namespace Chimera {

    template<typename T>
    struct Handle {
        uint32_t id = 0xFFFFFFFF;

        Handle() = default;
        explicit Handle(uint32_t id) : id(id) {}

        bool IsValid() const { return id != 0xFFFFFFFF; }
        bool operator==(const Handle& other) const { return id == other.id; }
        bool operator!=(const Handle& other) const { return id != other.id; }
        
        operator uint32_t() const { return id; }
    };

    // 预定义常用句柄类型
    class Image;
    class Buffer;
    class Material;
    
    using TextureHandle = Handle<Image>;
    using BufferHandle  = Handle<Buffer>;
    using MaterialHandle = Handle<Material>;
    
    template<typename T> class ResourceRef;
    using TextureRef = ResourceRef<Image>;
    using BufferRef  = ResourceRef<Buffer>;
    using MaterialRef = ResourceRef<Material>;

    /**
     * @brief RAII wrapper for handles that automatically manages 
     * reference counting via ResourceManager.
     */
    template<typename T>
    class ResourceRef {
    public:
        ResourceRef() = default;
        ResourceRef(Handle<T> handle);
        ~ResourceRef();

        ResourceRef(const ResourceRef& other);
        ResourceRef& operator=(const ResourceRef& other);

        ResourceRef(ResourceRef&& other) noexcept;
        ResourceRef& operator=(ResourceRef&& other) noexcept;

        Handle<T> Get() const { return m_Handle; }
        bool IsValid() const { return m_Handle.IsValid(); }

        Handle<T> operator->() const { return m_Handle; }
        operator Handle<T>() const { return m_Handle; }

    private:
        Handle<T> m_Handle;
    };

    // Forward declaration of ResourceManager to avoid circular dependency
    class ResourceManager;

    template<typename T>
    inline ResourceRef<T>::ResourceRef(Handle<T> handle) : m_Handle(handle) {
        if (m_Handle.IsValid()) AddRefInternal(m_Handle);
    }

    template<typename T>
    inline ResourceRef<T>::~ResourceRef() {
        if (m_Handle.IsValid()) ReleaseInternal(m_Handle);
    }

    template<typename T>
    inline ResourceRef<T>::ResourceRef(const ResourceRef& other) : m_Handle(other.m_Handle) {
        if (m_Handle.IsValid()) AddRefInternal(m_Handle);
    }

    template<typename T>
    inline ResourceRef<T>& ResourceRef<T>::operator=(const ResourceRef& other) {
        if (this != &other) {
            if (m_Handle.IsValid()) ReleaseInternal(m_Handle);
            m_Handle = other.m_Handle;
            if (m_Handle.IsValid()) AddRefInternal(m_Handle);
        }
        return *this;
    }

    template<typename T>
    inline ResourceRef<T>::ResourceRef(ResourceRef&& other) noexcept : m_Handle(other.m_Handle) {
        other.m_Handle = Handle<T>();
    }

    template<typename T>
    inline ResourceRef<T>& ResourceRef<T>::operator=(ResourceRef&& other) noexcept {
        if (this != &other) {
            if (m_Handle.IsValid()) ReleaseInternal(m_Handle);
            m_Handle = other.m_Handle;
            other.m_Handle = Handle<T>();
        }
        return *this;
    }

    // Since ResourceHandle.h is included in ResourceManager.h, we use a helper 
    // to avoid needing the full ResourceManager definition here.
    void AddRefInternal(Handle<Image> handle);
    void ReleaseInternal(Handle<Image> handle);
    void AddRefInternal(Handle<Buffer> handle);
    void ReleaseInternal(Handle<Buffer> handle);
    void AddRefInternal(Handle<Material> handle);
    void ReleaseInternal(Handle<Material> handle);

}
