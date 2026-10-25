AudioSignalResClass {
 Inputs {
  IOPItemInputClass {
   id 1
   name "RoomSize"
   comment "Per-stream input. The engine copies the SPEAKER entity's RoomSize signal into this stream every packet; EC29 writes 10*ear + 9*volume there (ear 1 both, 2 right, 3 left). 0 = not encoded -> both ears, full volume."
   tl -512 0
   children {
    10 11 12 20
   }
   valueMax 100
  }
  IOPInputValueClass {
   id 2
   name "Volume Curve Exponent"
   tl -512 384
   children {
    23
   }
   value 2.5
  }
 }
 Ops {
  IOPItemOpConvertorClass {
   id 10
   name "LeftVol"
   tl 0 -192
   children {
    30
   }
   inputs {
    ConnectionClass "1:0" {
     id 1
     port 0
    }
   }
   Default 0
   Intervals {
    IOPItemOpConvertorRange Both {
     min 10
     max 20
     out 1
    }
    IOPItemOpConvertorRange Right {
     min 20
     max 30
     out 0
    }
    IOPItemOpConvertorRange Left {
     min 30
     max 40
     out 1
    }
   }
  }
  IOPItemOpConvertorClass {
   id 11
   name "RightVol"
   tl 0 -64
   children {
    31
   }
   inputs {
    ConnectionClass "1:0" {
     id 1
     port 0
    }
   }
   Default 1
   Intervals {
    IOPItemOpConvertorRange Both {
     min 10
     max 20
     out 1
    }
    IOPItemOpConvertorRange Right {
     min 20
     max 30
     out 1
    }
    IOPItemOpConvertorRange Left {
     min 30
     max 40
     out 0
    }
   }
  }
  IOPItemOpConvertorClass {
   id 12
   name "EarBase"
   comment "Tens digit back out, so Sub leaves 9*volume"
   tl 0 128
   children {
    20
   }
   inputs {
    ConnectionClass "1:0" {
     id 1
     port 0
    }
   }
   Default -9
   Intervals {
    IOPItemOpConvertorRange Both {
     min 10
     max 20
     out 10
    }
    IOPItemOpConvertorRange Right {
     min 20
     max 30
     out 20
    }
    IOPItemOpConvertorRange Left {
     min 30
     max 40
     out 30
    }
   }
  }
  IOPItemOpSubClass {
   id 20
   name "Sub VolumeSteps"
   comment "port0 - port1 = 9*volume (or 9 when nothing is encoded)"
   tl 256 128
   children {
    21
   }
   inputs {
    ConnectionClass "1:0" {
     id 1
     port 0
    }
    ConnectionClass "12:1" {
     id 12
     port 1
    }
   }
  }
  IOPItemOpInterpolateClass {
   id 21
   name "Steps To 0..1"
   tl 512 128
   children {
    23
   }
   inputs {
    ConnectionClass "20:0" {
     id 20
     port 0
    }
   }
   "X max" 9
  }
  IOPItemOpPowClass {
   id 23
   name "Volume Curve"
   tl 768 128
   children {
    32
   }
   inputs {
    ConnectionClass "21:0" {
     id 21
     port 0
    }
    ConnectionClass "2:1" {
     id 2
     port 1
    }
   }
  }
 }
 Outputs {
  IOPItemOutputClass {
   id 30
   name "LeftVol"
   tl 1024 -192
   input 10
  }
  IOPItemOutputClass {
   id 31
   name "RightVol"
   tl 1024 -64
   input 11
  }
  IOPItemOutputClass {
   id 32
   name "ChannelVol"
   tl 1024 128
   input 23
  }
 }
 Input_Order {
  ItemDetailListItemClass RoomSize {
   Name "RoomSize"
   Id 1
  }
 }
 Output_Order {
  ItemDetailListItemClass LeftVol {
   Name "LeftVol"
   Id 30
  }
  ItemDetailListItemClass RightVol {
   Name "RightVol"
   Id 31
  }
  ItemDetailListItemClass ChannelVol {
   Name "ChannelVol"
   Id 32
  }
 }
}
