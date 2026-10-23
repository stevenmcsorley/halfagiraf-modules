from pathlib import Path
import shutil
product=Path(__file__).resolve().parent
family=product.parent.parent
shutil.copytree(family/'res/sonavyre',product/'res/sonavyre',dirs_exist_ok=True)
shutil.copy2(family/'LICENSE',product/'LICENSE')
shutil.copytree(family/'docs/sonavyre',product/'legal',dirs_exist_ok=True)
