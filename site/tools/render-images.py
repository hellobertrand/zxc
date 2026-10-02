"""Render the social preview image from tools/og.html."""
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
        await b.close()

asyncio.run(main())
