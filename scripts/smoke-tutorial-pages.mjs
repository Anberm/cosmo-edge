import { existsSync, readFileSync } from 'node:fs'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const repositoryRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..')
const distRoot = resolve(repositoryRoot, process.argv[2] ?? 'docs/.vitepress/dist')

const pages = [
  {
    html: 'tutorials/01-quickstart/quickstart.html',
    title: '快速开始：部署、登录与首次检测',
    lang: 'zh-CN',
    nav: '系统使用',
    group: '基础使用'
  },
  {
    html: 'tutorials/02-scenario-config/scenario-config.html',
    title: '场景任务配置：通道、区域、参数与告警',
    lang: 'zh-CN',
    nav: '系统使用',
    group: '基础使用'
  },
  {
    html: 'tutorials/03-vlm-guide/vlm-guide.html',
    title: 'VLM 与 DINO：提示词驱动的视觉任务',
    lang: 'zh-CN',
    nav: '系统使用',
    group: 'AI 能力配置'
  },
  {
    html: 'tutorials/04-pipeline-orchestration/pipeline-orchestration.html',
    title: '算法编排：修改与创建 Pipeline',
    lang: 'zh-CN',
    nav: '系统使用',
    group: '高级扩展'
  },
  {
    html: 'tutorials/05-model-porting/model-porting.html',
    title: '第三方模型接入：转换、上传与验证',
    lang: 'zh-CN',
    nav: '系统使用',
    group: '高级扩展'
  },
  {
    html: 'en/tutorials/01-quickstart/quickstart.html',
    title: 'Quick Start: Deployment, Sign-In, and First Detection',
    lang: 'en-US',
    nav: 'Using CosmoEdge',
    group: 'Basic Use'
  },
  {
    html: 'en/tutorials/02-scenario-config/scenario-config.html',
    title: 'Scenario Task Configuration: Channels, Regions, Parameters, and Alarms',
    lang: 'en-US',
    nav: 'Using CosmoEdge',
    group: 'Basic Use'
  },
  {
    html: 'en/tutorials/03-vlm-guide/vlm-guide.html',
    title: 'VLM and DINO: Prompt-Driven Vision Tasks',
    lang: 'en-US',
    nav: 'Using CosmoEdge',
    group: 'AI Capability Configuration'
  },
  {
    html: 'en/tutorials/04-pipeline-orchestration/pipeline-orchestration.html',
    title: 'Pipeline Orchestration: Modify and Create Pipelines',
    lang: 'en-US',
    nav: 'Using CosmoEdge',
    group: 'Advanced Extensions'
  },
  {
    html: 'en/tutorials/05-model-porting/model-porting.html',
    title: 'Third-Party Model Integration: Convert, Upload, and Validate',
    lang: 'en-US',
    nav: 'Using CosmoEdge',
    group: 'Advanced Extensions'
  }
]

const failures = []

function decodeHtml(value) {
  return value
    .replace(/<[^>]+>/gu, '')
    .replace(/&quot;/gu, '"')
    .replace(/&#39;|&apos;/gu, "'")
    .replace(/&amp;/gu, '&')
    .replace(/&lt;/gu, '<')
    .replace(/&gt;/gu, '>')
    .replace(/\u200B/gu, '')
    .replace(/\s+/gu, ' ')
    .trim()
}

for (const page of pages) {
  const file = join(distRoot, page.html)
  if (!existsSync(file)) {
    failures.push(`${page.html}: rendered page is missing`)
    continue
  }

  const html = readFileSync(file, 'utf8')
  if (!new RegExp(`<html[^>]+lang="${page.lang}"`, 'u').test(html)) {
    failures.push(`${page.html}: expected html lang="${page.lang}"`)
  }

  const title = html.match(/<title>([\s\S]*?)<\/title>/u)
  if (!title || !decodeHtml(title[1]).startsWith(page.title)) {
    failures.push(`${page.html}: document title does not start with "${page.title}"`)
  }

  const doc = html.match(
    /<div class="content-container"[^>]*>([\s\S]*?)<footer class="VPDocFooter/u
  )
  if (!doc) {
    failures.push(`${page.html}: VitePress document body was not found`)
    continue
  }

  const headings = [...doc[1].matchAll(/<h([1-6])\b[^>]*>([\s\S]*?)<\/h\1>/gu)]
  const h1 = headings.filter((heading) => heading[1] === '1')
  if (h1.length !== 1) {
    failures.push(`${page.html}: expected one rendered H1, found ${h1.length}`)
  } else if (decodeHtml(h1[0][2]) !== page.title) {
    failures.push(`${page.html}: rendered H1 is not "${page.title}"`)
  }

  if (headings.length === 0 || headings[0][1] !== '1') {
    failures.push(`${page.html}: first rendered heading is not H1; frontmatter may have leaked into content`)
  }
  if (
    headings.some(
      (heading) =>
        heading[1] === '2' && /^(?:title|description|prev|next)\s*:/iu.test(decodeHtml(heading[2]))
    )
  ) {
    failures.push(`${page.html}: frontmatter keys leaked into rendered headings`)
  }

  if (!html.includes(page.nav)) failures.push(`${page.html}: navigation label "${page.nav}" is missing`)
  if (!html.includes(page.group)) failures.push(`${page.html}: sidebar group "${page.group}" is missing`)
}

if (failures.length > 0) {
  console.error(`Tutorial page smoke test failed with ${failures.length} issue(s):`)
  for (const failure of failures) console.error(`- ${failure}`)
  process.exit(1)
}

console.log(`Tutorial page smoke test passed: ${pages.length} rendered bilingual pages.`)
