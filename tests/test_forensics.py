import unittest,sys,struct,zlib,hashlib,json,subprocess,tempfile,random
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from firmware_extract import KNOWN,validate,inflate_at
class Forensics(unittest.TestCase):
 def test_round_trip_and_trailer(self):
  b=b'actual data '*2048;c=zlib.compressobj(wbits=-15);blob=c.compress(b)+c.flush();end,out=inflate_at(blob+struct.pack('<I',len(b)),0);self.assertEqual(out,b);self.assertEqual(end,len(blob))
  with self.assertRaises(ValueError):inflate_at(blob+struct.pack('<I',len(b)+1),0)
  with self.assertRaises(ValueError):inflate_at(blob[:-1],0)
 def test_output_bound(self):
  c=zlib.compressobj(wbits=-15);b=b'x'*10000;blob=c.compress(b)+c.flush()+struct.pack('<I',len(b))
  with self.assertRaises(ValueError):inflate_at(blob,0,100)
 def test_wrong_original_rejected(self):
  with self.assertRaises(ValueError):validate(b'\0'*0x800010)
 def test_bounded_random_inputs(self):
  rng=random.Random(4214)
  for _ in range(400):
   b=rng.randbytes(rng.randrange(1,512))
   try:end,out=inflate_at(b,0,8192);self.assertLessEqual(len(out),8192);self.assertEqual(struct.unpack_from('<I',b,end)[0],len(out))
   except (ValueError,zlib.error):pass
if __name__=='__main__':unittest.main()
