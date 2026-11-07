#pragma once
// Decode real queued commands and scatter/gather backing as QEMU's 2D path
// does. A CPU framebuffer snapshot alone cannot detect an empty host scanout.
struct MockGpuImage {
    uint32_t w=0,h=0;
    std::vector<uint32_t> pixels;
    std::vector<VirtioGpuMemEntry> backing;
    bool complete=false;
};
struct MockGpuVisible {
    uint32_t command,resource,w,h;
    bool complete;
    std::vector<uint32_t> pixels;
};
struct MockGpuScanout {
    bool enabled=false;
    uint32_t resource=0;
    MockGpuImage images[3];
    std::vector<MockGpuVisible> visible;
    std::vector<VirtioGpuRect> transfer_rects;
    std::vector<VirtioGpuRect> flush_rects;
    bool invalid=false;
    unsigned display_resets=0;
    void reset(bool on=true) { *this={};enabled=on; }
    void reset_display() {
        if(!enabled)return;
        ++display_resets;resource=0;
        for(auto& image:images)image={};
        // QEMU replaces the console with NULL even when zero was written to
        // an already-reset device. This has no TRANSFER/SETSCANOUT command.
        visible.push_back({0,0,0,0,false,{}});
    }
    bool copy_backing(const MockGpuImage& image,uint64_t offset,void* dst,size_t bytes) {
        auto* out=static_cast<uint8_t*>(dst);
        for(const auto& e:image.backing) {
            if(offset>=e.length){offset-=e.length;continue;}
            const size_t n=bytes<e.length-offset?bytes:size_t(e.length-offset);
            const auto* src=host_phys_to_virt(e.addr+offset);
            if(!src)return false;
            std::memcpy(out,src,n);out+=n;bytes-=n;offset=0;
            if(!bytes)return true;
        }
        return false;
    }
    void execute(const void* data,size_t size) {
        if(!enabled)return;
        const auto type=static_cast<const VirtioGpuCtrlHdr*>(data)->type;
        if(type==VIRTIO_GPU_CMD_RESOURCE_CREATE_2D&&size>=sizeof(VirtioGpuResourceCreate2d)) {
            const auto& r=*static_cast<const VirtioGpuResourceCreate2d*>(data);
            if(r.resource_id>=3){invalid=true;return;}
            auto& image=images[r.resource_id];image={};image.w=r.width;image.h=r.height;
            image.pixels.assign(size_t(image.w)*image.h,0);
        } else if(type==VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING&&size>=sizeof(VirtioGpuResourceAttachBacking)) {
            const auto& r=*static_cast<const VirtioGpuResourceAttachBacking*>(data);
            if(r.resource_id>=3||size<sizeof(r)+size_t(r.nr_entries)*sizeof(VirtioGpuMemEntry)){invalid=true;return;}
            auto* entries=reinterpret_cast<const VirtioGpuMemEntry*>(static_cast<const uint8_t*>(data)+sizeof(r));
            images[r.resource_id].backing.assign(entries,entries+r.nr_entries);
        } else if(type==VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D&&size>=sizeof(VirtioGpuTransferToHost2d)) {
            const auto& r=*static_cast<const VirtioGpuTransferToHost2d*>(data);
            if(r.resource_id>=3){invalid=true;return;}
            auto& image=images[r.resource_id];
            transfer_rects.push_back(r.r);
            if(uint64_t(r.r.x)+r.r.width>image.w||uint64_t(r.r.y)+r.r.height>image.h){invalid=true;return;}
            for(unsigned row=0;row<r.r.height;++row)
                if(!copy_backing(image,r.offset+uint64_t(row)*image.w*4,
                    image.pixels.data()+size_t(r.r.y+row)*image.w+r.r.x,size_t(r.r.width)*4))invalid=true;
            if(!invalid&&r.r.x==0&&r.r.y==0&&r.r.width==image.w&&r.r.height==image.h)image.complete=true;
        } else if(type==VIRTIO_GPU_CMD_SET_SCANOUT&&size>=sizeof(VirtioGpuSetScanout)) {
            const auto& r=*static_cast<const VirtioGpuSetScanout*>(data);resource=r.resource_id;
            if(resource>=3){invalid=true;return;}
            if(resource){const auto& i=images[resource];visible.push_back({type,resource,i.w,i.h,i.complete,i.pixels});}
        } else if(type==VIRTIO_GPU_CMD_RESOURCE_FLUSH&&size>=sizeof(VirtioGpuResourceFlush)) {
            const auto& r=*static_cast<const VirtioGpuResourceFlush*>(data);
            flush_rects.push_back(r.r);
            if(r.resource_id==resource&&resource){const auto& i=images[resource];visible.push_back({type,resource,i.w,i.h,i.complete,i.pixels});}
        } else if(type==VIRTIO_GPU_CMD_RESOURCE_UNREF&&size>=sizeof(VirtioGpuResourceUnref)) {
            const auto& r=*static_cast<const VirtioGpuResourceUnref*>(data);
            if(r.resource_id>=3){invalid=true;return;}
            if(resource==r.resource_id){resource=0;invalid=true;} // visible resource freed too early
            images[r.resource_id]={};
        }
    }
};
static MockGpuScanout gpu_scanout_model;
