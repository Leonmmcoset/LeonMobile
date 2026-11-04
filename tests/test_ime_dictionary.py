"""Validate normalization and reproducible generation without network downloads."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile

REPO=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('pinyin_builder',REPO/'scripts/build-pinyin.py')
builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)

class DictionaryTests(unittest.TestCase):
    def test_normalization_common_words_and_reproducibility(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);archive=root/'unihan.zip'
            with zipfile.ZipFile(archive,'w') as z:
                z.writestr('Unihan_Readings.txt',
                    '# fixture\nU+5973\tkMandarin\tnǚ\nU+7EFF\tkMandarin\tlǜ\n'
                    'U+884C\tkHanyuPinyin\t123.4:háng,xíng\n')
            first=root/'first.tsv';second=root/'second.tsv'
            builder.generate(archive,first);builder.generate(archive,second)
            self.assertEqual(first.read_bytes(),second.read_bytes())
            data=first.read_text();self.assertIn('nv\t女\n',data);self.assertIn('lv\t绿\n',data)
            self.assertIn('hang\t行\n',data);self.assertIn('xing\t行\n',data)
            self.assertIn('nihao\t你好\n',data)
            keys=[line.split('\t')[0] for line in data.splitlines()]
            self.assertEqual(keys,sorted(keys))
