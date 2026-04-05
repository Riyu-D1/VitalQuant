import { useMemo } from 'react';
import { CodeTab, PreviewTab, TabsLayout } from '../../components/common/TabsLayout';
import { Box, Flex, Text } from '@chakra-ui/react';

import OpenInStudioButton from '../../components/common/Preview/OpenInStudioButton';
import useComponentProps from '../../hooks/useComponentProps';
import { ComponentPropsProvider } from '../../components/context/ComponentPropsContext';
import CodeExample from '../../components/code/CodeExample';

import PropTable from '../../components/common/Preview/PropTable';
import Dependencies from '../../components/code/Dependencies';
import Customize from '../../components/common/Preview/Customize';
import PreviewSlider from '../../components/common/Preview/PreviewSlider';
import PreviewSwitch from '../../components/common/Preview/PreviewSwitch';
import BackgroundContent from '../../components/common/Preview/BackgroundContent';

import LiquidChrome from '../../content/Backgrounds/LiquidChrome/LiquidChrome';
import { liquidChrome } from '../../constants/code/Backgrounds/liquidChromeCode';

const DEFAULT_PROPS = {
  speed: 0.3,
  baseColor: [0.1, 0.1, 0.1],
  interactive: true,
  amplitude: 0.3
};

const LiquidChromeDemo = () => {
  const { props, updateProp, resetProps, hasChanges } = useComponentProps(DEFAULT_PROPS);
  const { speed, baseColor, interactive, amplitude } = props;

  const propData = useMemo(
    () => [
      {
        name: 'baseColor',
        type: 'RGB array (number[3])',
        default: '[0.1, 0.1, 0.1]',
        description: 'Base color of the component. Specify as an RGB array.'
      },
      {
        name: 'speed',
        type: 'number',
        default: '1.0',
        description: 'Animation speed multiplier.'
      },
      {
        name: 'amplitude',
        type: 'number',
        default: '0.6',
        description: 'Amplitude of the distortion.'
      },
      {
        name: 'frequencyX',
        type: 'number',
        default: '2.5',
        description: 'Frequency modifier for the x distortion.'
      },
      {
        name: 'frequencyY',
        type: 'number',
        default: '1.5',
        description: 'Frequency modifier for the y distortion.'
      },
      {
        name: 'interactive',
        type: 'boolean',
        default: 'true',
        description: 'Enable mouse/touch interaction.'
      }
    ],
    []
  );

  return (
    <ComponentPropsProvider resetProps={resetProps} hasChanges={hasChanges}>
      <TabsLayout>
        <PreviewTab>
          <Box position="relative" className="demo-container" h={600} p={0} overflow="hidden">
            <LiquidChrome baseColor={baseColor} amplitude={amplitude} speed={speed} interactive={interactive} />

            <BackgroundContent pillText="New Background" headline="Swirl around in the deep sea of liquid chrome!" />
          </Box>

          <Flex justify="flex-end" mt={2} mb={-2}>
            <OpenInStudioButton
              backgroundId="liquid-chrome"
              currentProps={{ speed, amplitude, interactive }}
              defaultProps={{ speed: 0.2, amplitude: 0.3, frequencyX: 3, frequencyY: 3, interactive: true }}
            />
          </Flex>

          <Customize>
            <Text fontSize="sm">Colors</Text>
            <Flex gap={4} wrap="wrap">
              <Flex gap={4} align="center" mt={2} background="#170D27" px={4} borderRadius={16} position="relative">
                <PreviewSlider
                  min={0}
                  max={1}
                  width={50}
                  step={0.1}
                  value={baseColor[0]}
                  title="Red"
                  onChange={val => {
                    const newColors = [...baseColor];
                    newColors[0] = val;
                    updateProp('baseColor', newColors);
                  }}
                />
              </Flex>

              <Flex gap={4} align="center" mt={2} background="#170D27" px={4} borderRadius={16} position="relative">
                <PreviewSlider
                  min={0}
                  max={1}
                  width={50}
                  step={0.1}
                  value={baseColor[1]}
                  title="Green"
                  onChange={val => {
                    const newColors = [...baseColor];
                    newColors[1] = val;
                    updateProp('baseColor', newColors);
                  }}
                />
              </Flex>

              <Flex gap={4} align="center" mt={2} background="#170D27" px={4} borderRadius={16} position="relative">
                <PreviewSlider
                  min={0}
                  max={1}
                  width={50}
                  step={0.1}
                  value={baseColor[2]}
                  title="Blue"
                  onChange={val => {
                    const newColors = [...baseColor];
                    newColors[2] = val;
                    updateProp('baseColor', newColors);
                  }}
                />
              </Flex>
            </Flex>

            <PreviewSlider
              min={0}
              title="Speed"
              max={5}
              step={0.01}
              value={speed}
              onChange={val => updateProp('speed', val)}
            />

            <PreviewSlider
              min={0.1}
              title="Amplitude"
              max={1}
              step={0.01}
              value={amplitude}
              onChange={val => updateProp('amplitude', val)}
            />

            <PreviewSwitch
              title="Enable Interaction"
              isChecked={interactive}
              onChange={checked => updateProp('interactive', checked)}
            />
          </Customize>

          <PropTable data={propData} />
          <Dependencies dependencyList={['ogl']} />
        </PreviewTab>

        <CodeTab>
          <CodeExample codeObject={liquidChrome} componentName="LiquidChrome" />
        </CodeTab>
      </TabsLayout>
    </ComponentPropsProvider>
  );
};

export default LiquidChromeDemo;
