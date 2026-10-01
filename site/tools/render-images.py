"""Render the social preview image and the touch icon from tools/og.html and the favicon."""
import asyncio, pathlib
from playwright.async_api import async_playwright

HERE = pathlib.Path(__file__).resolve().parent
ASSETS = HERE.parent / "src" / "assets"

async def main():
    async with async_playwright() as p:
        b = await p.chromium.launch()
        pg = await b.new_page(viewport={"width": 1200, "height": 630})
        await pg.goto((HERE / "og.html").as_uri(), wait_until="networkidle")
        await pg.screenshot(path=str(ASSETS / "og.png"))
        pg = await b.new_page(viewport={"width": 180, "height": 180})
        svg = (ASSETS / "favicon.svg").read_text()
        await pg.set_content(f'<body style="margin:0">{svg.replace("<svg ", "<svg width=180 height=180 ")}</body>')
        await pg.screenshot(path=str(ASSETS / "apple-touch-icon.png"))
        await b.close()

asyncio.run(main())
