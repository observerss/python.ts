#!/usr/bin/env node
// 获取python编译参数
const { execSync } = require('child_process')

function getIncludeDirs () {
  if (process.platform === 'win32') {
    return [`${process.arch}/include`]
  } else if (process.platform === 'linux' || process.platform === 'darwin') {
    const includes = execSync('python3-config --includes', { encoding: 'utf-8' })
      .split(' ')
      .map((x) => {
        return x.replace(/(^-I|\n$)/g, '')
      })
    return Array.from(new Set(includes)).join(' ')
  } else {
    throw Error(`Unsupported platform: ${process.platform}`)
  }
}

function getLibraries () {
  if (process.platform === 'win32') {
    return ''
  } else if (process.platform === 'linux' || process.platform === 'darwin') {
    let libs
    const pver = execSync('python3 -V', { encoding: 'utf-8' }).split(' ')[1]
    const minor = pver.split('.')[1]
    if (parseInt(minor) >= 8) {
      libs = execSync('python3-config --libs --embed', { encoding: 'utf-8' })
    } else {
      libs = execSync('python3-config --libs', { encoding: 'utf-8' })
    }
    libs = libs
      .split(' ')
      .filter((x) => {
        return x.trim()
      })
    libs = Array.from(new Set(libs))
    if (process.platform === 'linux') {
      // 不把这个加上的话加载的时候会找不到PyFloatType等各种奇怪
      const shares = execSync('python3-config --configdir', { encoding: 'utf-8' })
        .replace(/\/config-.*\n$/, '/lib-dynload/*.so')
      libs.push(shares)
    }
    return libs.join(' ')
  } else {
    throw Error(`Unsupported platform: ${process.platform}`)
  }
}

function getLibraryDirs () {
  if (process.platform === 'win32') {
    return [`${process.arch}/libs`]
  } else if (process.platform === 'linux' || process.platform === 'darwin') {
    const ld = execSync('python3-config --ldflags', { encoding: 'utf-8' })
      .split(' ')
      .filter((x) => {
        return x.startsWith('-L')
      })
      .map((x) => {
        return x.slice(2)
      })
    return ld.join(' ')
  } else {
    throw Error(`Unsupported platform: ${process.platform}`)
  }
}

module.exports = {
  include_dirs: getIncludeDirs(),
  libraries: getLibraries(),
  library_dirs: getLibraryDirs()
}
