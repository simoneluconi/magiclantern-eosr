' Enable the camera boot flag on EOS R6 1.9.0 only (Canon Basic).
' Writes B:/BOOTFLAG.TXT first, so a missing file means the script did not run.
' Based on:
'   https://github.com/lclevy/cbasic_examples/blob/main/Universal/extend_bootdisk.m
'   https://github.com/lclevy/cbasic_examples/blob/main/Universal/extend_fw_sign.m (tested on R6.150)
'   https://github.com/lclevy/cbasic_examples/blob/main/RP.160/extend_checksig.m (Coon)

dim expectedSig     = 0xE95FEB0E   ' SIG_R6_190 in src/fw-signature.h
dim sigLen          = 0x10000
dim pRomBaseAddress = 0xE0100000

private sub compute_signature()
  p = pRomBaseAddress
  c = 0

  For i = 0 To (sigLen - 1)
    c = c + *p
    p = p + 4
  Next

  compute_signature = c
end sub

private sub Initialize()
  fileName = "B:/BOOTFLAG.TXT"
  RemoveFile(fileName)
  f = OpenFileCREAT(fileName)
  CloseFile(f)
  f = OpenFileWR(fileName)

  sign = compute_signature()
  WriteFileString(f, "fw signature: 0x%08x (expected 0x%08x)\n", sign, expectedSig)

  if sign = expectedSig Then
    EnableBootDisk()
    WriteFileString(f, "EnableBootDisk() called\n")
  else
    WriteFileString(f, "signature mismatch, boot flag NOT changed\n")
  end if

  CloseFile(f)
end sub
