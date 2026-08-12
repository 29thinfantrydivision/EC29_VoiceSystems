AudioSignalResClass {
 Inputs {
  IOPItemInputClass {
   id 1
   name "TransmissionQuality"
   tl -403.039 93.931
   children {
    6 7 14 19 21 23
   }
   value 1
  }
  IOPInputValueClass {
   id 9
   name "Vol Min [dB]"
   tl -347.234 378.182
   children {
    10
   }
   value -60
  }
  IOPInputValueClass {
   id 16
   name "Vol Min [dB]"
   tl -345.795 550
   children {
    17
   }
   value -10
  }
 }
 Ops {
  IOPItemOpInterpolateClass {
   id 6
   name "Interpolate 1"
   tl 36 95
   children {
    5
   }
   inputs {
    ConnectionClass "1:0" {
     id 1
     port 0
    }
   }
   "Y min" 1
   "Y max" 0.2
  }
  IOPItemOpInterpolateClass {
   id 7
   name "Interpolate 1"
   tl 39.111 282
   children {
    8
   }
   inputs {
    ConnectionClass "10:4" {
     id 10
     port 4
    }
    ConnectionClass "1:0" {
     id 1
     port 0
    }
   }
   "X max" 0.8
   "Y min" 1.8
   "Fade In Type" "Power of 1/3"
   "Fade Out Type" "Power of 1/3"
  }
  SignalOpDb2GainClass {
   id 10
   name "Db2Gain 10"
   tl -157.273 378.182
   children {
    7
   }
   inputs {
    ConnectionClass "9:0" {
     id 9
     port 0
    }
   }
  }
  IOPItemOpInterpolateClass {
   id 14
   name "Interpolate 14"
   tl 41.528 477.639
   children {
    12
   }
   inputs {
    ConnectionClass "17:3" {
     id 17
     port 3
    }
    ConnectionClass "1:0" {
     id 1
     port 0
    }
   }
   "Y min" 1
  }
  SignalOpDb2GainClass {
   id 17
   name "Db2Gain 10"
   tl -155.593 548.75
   children {
    14
   }
   inputs {
    ConnectionClass "16:0" {
     id 16
     port 0
    }
   }
  }
  IOPItemOpInterpolateClass {
   id 19
   name "Interpolate 14"
   tl 32.581 748.374
   children {
    18
   }
   inputs {
    ConnectionClass "1:0" {
     id 1
     port 0
    }
   }
  }
  IOPItemOpInterpolateClass {
   id 21
   name "Interpolate 1"
   tl 42.662 -149.72
   children {
    20
   }
   inputs {
    ConnectionClass "1:0" {
     id 1
     port 0
    }
   }
   "Y min" 1000
   "Y max" 3600
  }
  IOPItemOpInterpolateClass {
   id 23
   name "Interpolate 1"
   tl 43.812 -379.72
   children {
    22
   }
   inputs {
    ConnectionClass "1:0" {
     id 1
     port 0
    }
   }
   "Y min" 3400
   "Y max" 1200
  }
 }
 Outputs {
  IOPItemOutputClass {
   id 5
   name "Quality_W"
   tl 227 95
   input 6
  }
  IOPItemOutputClass {
   id 8
   name "Noise_V"
   tl 227.658 281
   input 7
  }
  IOPItemOutputClass {
   id 12
   name "Radio_V"
   tl 227 477.287
   input 14
  }
  IOPItemOutputClass {
   id 18
   name "Voice_V"
   tl 247.953 749.243
   input 19
  }
  IOPItemOutputClass {
   id 20
   name "LP_Cutoff"
   tl 233.662 -149.72
   input 21
  }
  IOPItemOutputClass {
   id 22
   name "HP_Cutoff"
   tl 234.812 -380.87
   input 23
  }
 }
 compiled IOPCompiledClass "{672F7881628506F1}" {
  visited {
   261 519 133 263 5 391 262 135 134 7 6
  }
  ins {
   IOPCompiledIn "{672F7881628506EB}" {
    data {
     3 3 65539 196611
    }
   }
   IOPCompiledIn "{672F7881628506D5}" {
    data {
     1 131075
    }
   }
   IOPCompiledIn "{672F7881628506DB}" {
    data {
     1 262147
    }
   }
  }
  ops {
   IOPCompiledOp "{672F7881628506CB}" {
    data {
     1 2 2 0 0
    }
   }
   IOPCompiledOp "{672F788162850638}" {
    data {
     1 65538 4 131073 4 0 0
    }
   }
   IOPCompiledOp "{672F788162850620}" {
    data {
     1 65539 2 65536 0
    }
   }
   IOPCompiledOp "{672F788162850629}" {
    data {
     1 131074 4 262145 3 0 0
    }
   }
   IOPCompiledOp "{672F78816285062D}" {
    data {
     1 196611 2 131072 0
    }
   }
  }
  outs {
   IOPCompiledOut "{672F78816285061A}" {
    data {
     0
    }
   }
   IOPCompiledOut "{672F788162850600}" {
    data {
     0
    }
   }
   IOPCompiledOut "{672F788162850602}" {
    data {
     0
    }
   }
  }
  processed 11
  version 2
 }
 Input_Order {
  ItemDetailListItemClass TransmissionQuality {
   Name "TransmissionQuality"
   Id 1
  }
 }
 Output_Order {
  ItemDetailListItemClass Quality_W {
   Name "Quality_W"
   Id 5
  }
  ItemDetailListItemClass Noise_V {
   Name "Noise_V"
   Id 8
  }
  ItemDetailListItemClass Radio_V {
   Name "Radio_V"
   Id 12
  }
  ItemDetailListItemClass Voice_V {
   Name "Voice_V"
   Id 18
  }
  ItemDetailListItemClass LP_Cutoff {
   Name "LP_Cutoff"
   Id 20
  }
  ItemDetailListItemClass HP_Cutoff {
   Name "HP_Cutoff"
   Id 22
  }
 }
}