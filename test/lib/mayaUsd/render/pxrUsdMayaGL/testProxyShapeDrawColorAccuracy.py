#!/usr/bin/env mayapy
#
# Copyright 2018 Pixar
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

from maya import cmds

import os
import sys
import unittest

import fixturesUtils


class testProxyShapeDrawColorAccuracy(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        # The test USD data is authored Z-up, so make sure Maya is configured
        # that way too.
        cmds.upAxis(axis='z')

        inputPath = fixturesUtils.setUpClass(__file__,
            initializeStandalone=False, loadPlugin=False)

        cls._testName = 'ProxyShapeDrawColorAccuracyTest'
        cls._inputDir = os.path.join(inputPath, cls._testName)

        cls._testDir = os.path.abspath('.')

        cls._cameraName = 'MainCamera'

    def setUp(self):
        cmds.file(new=True, force=True)

        # To control where the rendered images are written, we force Maya to
        # use the test directory as the workspace.
        cmds.workspace(self._testDir, o=True)

    def _WriteViewportImage(self, outputImageName, suffix):
        # Make sure the hardware renderer is available
        MAYA_RENDERER_NAME = 'mayaHardware2'
        mayaRenderers = cmds.renderer(query=True, namesOfAvailableRenderers=True)
        self.assertIn(MAYA_RENDERER_NAME, mayaRenderers)

        # Make it the current renderer.
        cmds.setAttr('defaultRenderGlobals.currentRenderer', MAYA_RENDERER_NAME,
            type='string')
        # Set the image format to PNG.
        cmds.setAttr('defaultRenderGlobals.imageFormat', 32)
        # Set the render mode to shaded and textured.
        cmds.setAttr('hardwareRenderingGlobals.renderMode', 4)
        # Specify the output image prefix. The path to it is built from the
        # workspace directory.
        cmds.setAttr('defaultRenderGlobals.imageFilePrefix',
            '%s_%s' % (outputImageName, suffix),
            type='string')
        # Apply the viewer's color transform to the rendered image, otherwise
        # it comes out too dark.
        cmds.setAttr("defaultColorMgtGlobals.outputTransformEnabled", 1)

        # Do the render.
        cmds.ogsRender(camera=self._cameraName, currentFrame=True, width=960,
            height=540)

    def testDrawAndToggleLightingMode(self):
        """
        Tests drawing a USD proxy shape node before and after changing the
        lighting mode between using the default lights and using all scene
        lights.

        This ensures that gamma correction is applied correctly in both
        lighting modes and that there is no additional scene ambient added.
        """
        mayaSceneFile = '%s.ma' % self._testName
        mayaSceneFullPath = os.path.join(self._inputDir, mayaSceneFile)
        cmds.file(mayaSceneFullPath, open=True, force=True)
        currentTime = cmds.playbackOptions(query=True, animationStartTime=True)

        # Force an initial draw to complete by switching frames.
        currentTime += 1
        cmds.currentTime(currentTime, edit=True)

        # Set the lighting mode to using the default lights.
        cmds.setAttr("hardwareRenderingGlobals.lightingMode", 0)

        self._WriteViewportImage(self._testName, 'default_lights')

        # Switch the lighting mode to use all scene lights.
        cmds.setAttr("hardwareRenderingGlobals.lightingMode", 1)

        self._WriteViewportImage(self._testName, 'all_lights')


if __name__ == '__main__':
    fixturesUtils.runTests(globals())
