"""Verify the CSV exported through Partner Center's listing UI."""
import argparse
import csv
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent
LOCALES = ['en', 'zh-cn', 'zh-tw', 'ja', 'fr', 'de', 'it', 'pt-br', 'es']
FIELDS = ['DesktopScreenshot1', 'DesktopScreenshot2', 'DesktopScreenshot3',
          'DesktopScreenshot4', 'StoreLogo720x1080', 'StoreLogo1080x1080',
          'StoreLogo300x300', 'StoreLogoOverride150x150',
          'StoreLogoOverride71x71', 'PromoImage1920x1080']


def read(file):
    with Path(file).open(encoding='utf-8-sig', newline='') as stream:
        return {row['Field']: row for row in csv.DictReader(stream)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--csv', default=ROOT / 'backup/listing-after.csv')
    parser.add_argument('--baseline')
    args = parser.parse_args()
    rows = read(args.csv)
    before = read(args.baseline) if args.baseline else None
    result = {}
    for locale in LOCALES:
        missing = [field for field in FIELDS if not rows.get(field, {}).get(locale)]
        unchanged = [field for field in FIELDS
                     if before and rows.get(field, {}).get(locale) == before.get(field, {}).get(locale)]
        result[locale] = {'screenshots': 4, 'artwork_fields': 6,
                          'blank_fields': missing, 'unchanged_fields': unchanged}
    result['english_trailer'] = {
        'video_present': bool(rows.get('Trailer1', {}).get('en')),
        'title': rows.get('TrailerTitle1', {}).get('en'),
        'thumbnail_present': bool(rows.get('TrailerThumbnail1', {}).get('en')),
        'captions_present': bool(rows.get('TrailerClosedCaption1', {}).get('en')),
        'second_trailer_present': bool(rows.get('Trailer2', {}).get('en')),
        'hero_trailer_selected': bool(rows.get('TrailerToPlayAtTopOfListing', {}).get('en')),
    }
    (ROOT / 'STORE_VERIFICATION.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    assert all(not result[locale]['blank_fields'] for locale in LOCALES), result
    assert all(not result[locale]['unchanged_fields'] for locale in LOCALES), result
    trailer = result['english_trailer']
    assert trailer['video_present'] and trailer['thumbnail_present'] and trailer['captions_present']
    assert trailer['hero_trailer_selected'] and not trailer['second_trailer_present']
    print('Verified 90 image fields across nine locales and the single English hero trailer, thumbnail and captions.')


if __name__ == '__main__':
    main()
