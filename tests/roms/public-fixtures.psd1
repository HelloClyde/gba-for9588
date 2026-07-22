@{
    Arm = @{
        Path = 'arm/arm.gba'
        Size = 8824
        SHA256 = '77ee88662552bdc885c1080c0172ff119d54db791bd73b21808cf1ff1fe5b40e'
        Category = 'CPU ARM'
        ExpectedScreen = 'Failed test 225 (upstream gpSP interpreter parity)'
        Headless120 = @{
            VideoHash = '3f2a3d75'
            VideoLastHash = '8af5f755'
            GbaPc = '08001ec4'
            GbaCpsr = '6000001f'
        }
    }
    Thumb = @{
        Path = 'thumb/thumb.gba'
        Size = 3680
        SHA256 = 'b5cb2291df4ab314b31c598acd9bff2ccfa0b38efff29daadfe97422ce369b67'
        Category = 'CPU Thumb'
        ExpectedScreen = 'Failed test 227 (MIPS DRC; interpreter stops at 211)'
        Headless120 = @{
            VideoHash = '3d7cf4b6'
            VideoLastHash = '10cfd0f6'
            GbaPc = '08000aac'
            GbaCpsr = '6000001f'
        }
    }
    Memory = @{
        Path = 'memory/memory.gba'
        Size = 2172
        SHA256 = '21024fb6aae6343f5f0466dd54e3149de1fbeb23f78e7d85a015c983684d2f87'
        Category = 'Memory'
        ExpectedScreen = 'All tests passed'
        Headless120 = @{
            VideoHash = '386e2c85'
            VideoLastHash = '58efe705'
            GbaPc = '080004c8'
            GbaCpsr = '6000001f'
        }
    }
    PpuHello = @{
        Path = 'ppu/hello.gba'
        Size = 1300
        SHA256 = '38aed48b67bc0f701e8aa222b0c3334bd306bd29888707bb7224d81f5576c264'
        Category = 'PPU text'
        ExpectedScreen = 'Hello world!'
        Headless120 = @{
            VideoHash = 'd25d5cc2'
            VideoLastHash = 'e28e1242'
            GbaPc = '08000160'
            GbaCpsr = '6000001f'
        }
    }
    PpuShades = @{
        Path = 'ppu/shades.gba'
        Size = 352
        SHA256 = '127ce348f17e4d5fd8a0821a7af757eda6109c66c9f7b8f7d1e0fa31831b407d'
        Category = 'PPU palette'
        ExpectedScreen = 'Blue shade gradient'
        Headless120 = @{
            VideoHash = '06ab3ac5'
            VideoLastHash = '3fe175c5'
            GbaPc = '0800015c'
            GbaCpsr = '6000001f'
        }
    }
    PpuStripes = @{
        Path = 'ppu/stripes.gba'
        Size = 324
        SHA256 = '80d6cc05b5f289b9a2392621fc245270f08e9f0d77fd33982120cef240f5a504'
        Category = 'PPU scanline'
        ExpectedScreen = 'Alternating blue vertical stripes'
        Headless120 = @{
            VideoHash = '9ca6f5c5'
            VideoLastHash = '3f7bbcc5'
            GbaPc = '08000140'
            GbaCpsr = '6000001f'
        }
    }
    SaveNone = @{
        Path = 'save/none.gba'
        Size = 1628
        SHA256 = 'edb34ba6590d070c8a50cf0f3566b1e3cc679377b978224ff1b872d27f2b1630'
        Category = 'Save none'
        ExpectedScreen = 'All tests passed'
        Headless120 = @{
            VideoHash = '386e2c85'
            VideoLastHash = '58efe705'
            GbaPc = '080002a8'
            GbaCpsr = '6000001f'
        }
    }
    SaveSram = @{
        Path = 'save/sram.gba'
        Size = 2084
        SHA256 = 'a37ad99c31e3f805eb05a00e498b65bd78e6f43a0a139cd695bea1f88229af2c'
        Category = 'Save SRAM'
        ExpectedScreen = 'Failed test 006 (upstream gpSP interpreter parity)'
        Headless120 = @{
            VideoHash = '3ed569a6'
            VideoLastHash = '1ee0fba6'
            GbaPc = '08000470'
            GbaCpsr = '6000001f'
        }
    }
    SaveFlash64 = @{
        Path = 'save/flash64.gba'
        Size = 3708
        SHA256 = '7e2aa32e943aedde88bd750eadcdbf55152d3a1ec61385011b7f15cd85b07c02'
        Category = 'Save Flash 64K'
        ExpectedScreen = 'Failed test 006 (upstream gpSP interpreter parity)'
        Headless120 = @{
            VideoHash = '3ed569a6'
            VideoLastHash = '1ee0fba6'
            GbaPc = '08000ac8'
            GbaCpsr = '6000001f'
        }
    }
    SaveFlash128 = @{
        Path = 'save/flash128.gba'
        Size = 4096
        SHA256 = '9ac50e51d3ce4209dbdf85e472e70c067d5827e9af1bb3e707f6bd9059d5f0c6'
        Category = 'Save Flash 128K'
        ExpectedScreen = 'Failed test 006 (upstream gpSP interpreter parity)'
        Headless120 = @{
            VideoHash = '3ed569a6'
            VideoLastHash = '1ee0fba6'
            GbaPc = '08000c4c'
            GbaCpsr = '6000001f'
        }
    }
}
