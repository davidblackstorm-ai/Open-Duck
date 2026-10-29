"""Deterministic setup layout using a real native JUCE render.

No plugin artwork is regenerated or painted over. The native screenshot is only
scaled/cropped for the setup's existing branding surfaces.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

root=Path(__file__).resolve().parents[1]
import argparse
parser=argparse.ArgumentParser(description='Export installer artwork from an actual 0.4.1 native render. Existing original setup audio is preserved.')
parser.add_argument('--preview',type=Path,required=True)
args=parser.parse_args()
preview=args.preview.resolve(strict=True)
im=Image.open(preview).convert('RGB')
im.resize((492,260),Image.Resampling.LANCZOS).save(root/'installer/Brand.bmp')
font_path=Path('C:/Windows/Fonts/bahnschrift.ttf')
def font(size):
    return ImageFont.truetype(str(font_path),size) if font_path.exists() else ImageFont.load_default()
card=Image.new('RGB',(344,628),(8,16,22)); d=ImageDraw.Draw(card)
for x in range(344):
    c=int(10+16*(1-x/344)); d.line((x,0,x,627),fill=(c,c+8,c+13))
d.line((24,26,320,26),fill=(0,215,240),width=3)
d.text((26,45),'BLACK CIRCUIT',font=font(21),fill=(164,191,206))

d.text((26,82),'OpenDuck',font=font(43),fill=(235,244,248))
d.text((26,132),'PRO X',font=font(51),fill=(0,225,255))
d.text((28,202),'PRECISION DUCKING',font=font(19),fill=(164,191,206))
thumb=im.copy(); thumb.thumbnail((320,162),Image.Resampling.LANCZOS)
card.paste(thumb,((344-thumb.width)//2,238))
d.text((28,441),'700 PRESET',font=font(28),fill=(235,244,248))
d.text((28,483),'BAND / SHIFT / SLOPE',font=font(20),fill=(145,192,209))
d.line((26,541,318,541),fill=(52,85,99),width=1)
d.text((28,565),'WINDOWS x64  /  VST3',font=font(18),fill=(0,219,246))
d.text((28,592),'VERSIONE 0.4.1',font=font(15),fill=(155,180,192))
card.save(root/'installer/WizardPanel.bmp')
# Small header uses the actual native chrome X, not a newly generated logo.
im.crop((int(im.width*.18),int(im.height*.135),int(im.width*.23),int(im.height*.235))).resize((64,64),Image.Resampling.LANCZOS).save(root/'installer/WizardSmall.bmp')
print('Setup assets exported from the real Premium native render; existing original audio preserved.')
