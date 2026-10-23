# Adreno 750 compute, render, discovery and timing checks.
# Requires the paired AIR library, RedwoodMetal.bundle and the two local iOS metallibs.
# The compute list excludes kernels with input-dependent unbounded loops.
/redwood/bin/rwtouch wait 60
/redwood/bin/metal_props on
/redwood/bin/metal_caps
/redwood/bin/metal_drv_test /redwood/share/metal/PortraitFilters.metallib MetalFaceMask_InitMinMax
/redwood/bin/metal_drv_test /redwood/share/metal/PortraitFilters.metallib MetalFaceMask_CalcCubeInputScaling
/redwood/bin/metal_drv_test /redwood/share/metal/PortraitFilters.metallib MetalFaceMask_InitSpanTable
/redwood/bin/metal_drv_test /redwood/share/metal/PortraitFilters.metallib MetalFaceMask_PropogateAssociations
/redwood/bin/metal_drv_test /redwood/share/metal/PortraitFilters.metallib fbs_bistochastize_init
/redwood/bin/metal_drv_test /redwood/share/metal/PortraitFilters.metallib fbs_pcg_iter2
/redwood/bin/metal_drv_test /redwood/share/metal/PortraitFilters.metallib fbs_pcg_iter3
/redwood/bin/metal_drv_test /redwood/share/metal/PortraitFilters.metallib image_calcminmax0
/redwood/bin/metal_drv_test /redwood/share/metal/PortraitFilters.metallib image_calcminmax2
/redwood/bin/metal_drv_test --draw /redwood/share/metal/AppleMetalGLRenderer.metallib
/redwood/bin/metal_drv_test --draw-index /redwood/share/metal/AppleMetalGLRenderer.metallib
/redwood/bin/metal_drv_test --draw --vertex texturedVertex2D --fragment texturedFragment2DUInt /redwood/share/metal/AppleMetalGLRenderer.metallib
/redwood/bin/metal_drv_test --draw --vertex clearVertex /redwood/share/metal/AppleMetalGLRenderer.metallib
/redwood/bin/metal_drv_test --draw-depth depth32float /redwood/share/metal/AppleMetalGLRenderer.metallib
/redwood/bin/metal_drv_test --draw-depth depth16unorm /redwood/share/metal/AppleMetalGLRenderer.metallib
/redwood/bin/metal_drv_test --draw-depth depth32float_stencil8 /redwood/share/metal/AppleMetalGLRenderer.metallib
/redwood/bin/metal_drv_test --draw-depth stencil8 /redwood/share/metal/AppleMetalGLRenderer.metallib
/redwood/bin/metal_drv_test --time 16 /redwood/share/metal/AppleMetalGLRenderer.metallib
/redwood/bin/triangle_app --layer
/redwood/bin/hello_dyn A750 METAL SUITE COMPLETE
