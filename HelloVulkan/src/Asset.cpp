#include <cassert>
#include "Asset.h"

#ifdef _ANDROID

#include <android/asset_manager.h>

static AAssetManager* gAssetManager = nullptr;

struct Asset::Impl
{
    Impl(std::string filename, uint32_t /*openMode*/)
    {
        assert(gAssetManager);
        mAsset = AAssetManager_open(gAssetManager, filename.c_str(), AASSET_MODE_UNKNOWN);
    }

    ~Impl()
    {
        if (mAsset)
        {
            close();
        }
    }

    uint32_t getLength() 
    {
        assert(mAsset);
        return AAsset_getLength(mAsset);
    }

    void read(uint8_t* data, uint32_t size)
    {
        AAsset_read(mAsset, data, size);
    }

    void close() 
    {
        assert(mAsset);
        AAsset_close(mAsset);
        mAsset = nullptr;
    }

    AAsset* mAsset{ nullptr };
};

void Asset::setAssetManager(void* assetManager)
{
    gAssetManager = reinterpret_cast<AAssetManager*>(assetManager);
}

Asset::Asset(std::string filename, uint32_t openMode)
{
    mImpl = std::make_unique<Impl>(filename, openMode);
}

Asset::~Asset()
{
    mImpl = nullptr;
}

uint32_t Asset::getLength()
{
    return mImpl->getLength();
}

void Asset::read(void* data, uint32_t size)
{
    return mImpl->read(reinterpret_cast<uint8_t*>(data), size);
}

void Asset::close()
{
    mImpl->close();
}

#endif
