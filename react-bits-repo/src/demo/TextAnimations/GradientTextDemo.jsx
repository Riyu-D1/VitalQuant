import { useMemo } from 'react';
import { Box, Text, Flex, Input, Icon } from '@chakra-ui/react';
import { Plus, X } from 'lucide-react';
import { CodeTab, PreviewTab, TabsLayout } from '../../components/common/TabsLayout';

import CodeExample from '../../components/code/CodeExample';
import PropTable from '../../components/common/Preview/PropTable';

import Customize from '../../components/common/Preview/Customize';
import PreviewSlider from '../../components/common/Preview/PreviewSlider';
import PreviewSwitch from '../../components/common/Preview/PreviewSwitch';
import PreviewSelect from '../../components/common/Preview/PreviewSelect';

import useComponentProps from '../../hooks/useComponentProps';
import { ComponentPropsProvider } from '../../components/context/ComponentPropsContext';

import GradientText from '../../content/TextAnimations/GradientText/GradientText';
import { gradientText } from '../../constants/code/TextAnimations/gradientTextCode';

const DEFAULT_PROPS = {
  colors: ['#5227FF', '#FF9FFC', '#B19EEF'],
  animationSpeed: 8,
  direction: 'horizontal',
  pauseOnHover: false,
  yoyo: true,
  showBorder: false
};

const GradientTextDemo = () => {
  const { props, updateProp, resetProps, hasChanges } = useComponentProps(DEFAULT_PROPS);
  const { colors, animationSpeed, direction, pauseOnHover, yoyo, showBorder } = props;

  const updateColor = (index, newColor) => {
    const newColors = [...colors];
    newColors[index] = newColor;
    updateProp('colors', newColors);
  };

  const addColor = () => {
    if (colors.length < 8) {
      updateProp('colors', [...colors, '#ffffff']);
    }
  };

  const removeColor = index => {
    if (colors.length > 2) {
      updateProp(
        'colors',
        colors.filter((_, i) => i !== index)
      );
    }
  };

  const propData = useMemo(
    () => [
      {
        name: 'children',
        type: 'ReactNode',
        default: '-',
        description: 'The content to be displayed inside the gradient text.'
      },
      {
        name: 'className',
        type: 'string',
        default: "''",
        description: 'Adds custom classes to the root element for additional styling.'
      },
      {
        name: 'colors',
        type: 'string[]',
        default: `["#5227FF", "#FF9FFC", "#B19EEF"]`,
        description: 'Array of colors for the gradient effect.'
      },
      {
        name: 'animationSpeed',
        type: 'number',
        default: '8',
        description: 'Duration of one animation cycle in seconds.'
      },
      {
        name: 'direction',
        type: `'horizontal' | 'vertical' | 'diagonal'`,
        default: `'horizontal'`,
        description: 'Direction of the gradient animation.'
      },
      {
        name: 'pauseOnHover',
        type: 'boolean',
        default: 'false',
        description: 'Pauses the animation when hovering over the text.'
      },
      {
        name: 'yoyo',
        type: 'boolean',
        default: 'true',
        description: 'Reverses animation direction at the end instead of looping.'
      },
      {
        name: 'showBorder',
        type: 'boolean',
        default: 'false',
        description: 'Displays a gradient border around the text.'
      }
    ],
    []
  );

  return (
    <ComponentPropsProvider props={props} defaultProps={DEFAULT_PROPS} resetProps={resetProps} hasChanges={hasChanges}>
      <TabsLayout>
        <PreviewTab>
          <Box position="relative" className="demo-container" minH={400}>
            <Text fontSize={'3rem'} as="div">
              <GradientText
                colors={colors}
                animationSpeed={animationSpeed}
                direction={direction}
                pauseOnHover={pauseOnHover}
                yoyo={yoyo}
                showBorder={showBorder}
              >
                Gradient Magic
              </GradientText>
            </Text>
          </Box>

          <Customize>
            <PreviewSlider
              title="Animation Speed"
              min={1}
              max={20}
              step={0.5}
              value={animationSpeed}
              onChange={val => updateProp('animationSpeed', val)}
              valueUnit="s"
            />

            <PreviewSelect
              title="Direction"
              options={[
                { value: 'horizontal', label: 'Horizontal' },
                { value: 'vertical', label: 'Vertical' },
                { value: 'diagonal', label: 'Diagonal' }
              ]}
              value={direction}
              onChange={val => updateProp('direction', val)}
            />

            <PreviewSwitch title="Yoyo" isChecked={yoyo} onChange={checked => updateProp('yoyo', checked)} />

            <PreviewSwitch
              title="Pause on Hover"
              isChecked={pauseOnHover}
              onChange={checked => updateProp('pauseOnHover', checked)}
            />

            <PreviewSwitch
              title="Show Border"
              isChecked={showBorder}
              onChange={checked => updateProp('showBorder', checked)}
            />

            <Box mt={4}>
              <Text fontSize="sm" mb={2}>
                Colors
              </Text>
              <Flex flexWrap="wrap" gap={2} px={1} pt={1}>
                {colors.map((color, index) => (
                  <Box key={index} position="relative" w="32px" h="32px">
                    <Box
                      w="32px"
                      h="32px"
                      borderRadius="6px"
                      bg={color}
                      border="2px solid #271E37"
                      overflow="hidden"
                      position="relative"
                    >
                      <Input
                        type="color"
                        value={color}
                        onChange={e => updateColor(index, e.target.value)}
                        position="absolute"
                        top={0}
                        left={0}
                        w="32px"
                        h="32px"
                        opacity={0}
                        cursor="pointer"
                      />
                    </Box>
                    {colors.length > 2 && (
                      <Flex
                        as="button"
                        onClick={() => removeColor(index)}
                        position="absolute"
                        top="-6px"
                        right="-6px"
                        w="16px"
                        h="16px"
                        bg="#170D27"
                        border="1px solid #271E37"
                        borderRadius="50%"
                        align="center"
                        justify="center"
                        cursor="pointer"
                      >
                        <Icon as={X} boxSize={2.5} color="#988BC7" />
                      </Flex>
                    )}
                  </Box>
                ))}
                {colors.length < 8 && (
                  <Flex
                    as="button"
                    onClick={addColor}
                    w="32px"
                    h="32px"
                    borderRadius="6px"
                    border="2px dashed #392e4e"
                    align="center"
                    justify="center"
                    cursor="pointer"
                    _hover={{ borderColor: '#5227FF' }}
                    transition="border-color 0.2s"
                  >
                    <Icon as={Plus} boxSize={4} color="#988BC7" />
                  </Flex>
                )}
              </Flex>
              <Box
                bg={`linear-gradient(to right, ${[...colors, colors[0]].join(', ')})`}
                w="100%"
                maxW="300px"
                h="12px"
                borderRadius="md"
                border="1px solid #271E37"
                mt={3}
              />
            </Box>
          </Customize>

          <PropTable data={propData} />
        </PreviewTab>

        <CodeTab>
          <CodeExample codeObject={gradientText} componentName="GradientText" />
        </CodeTab>
      </TabsLayout>
    </ComponentPropsProvider>
  );
};

export default GradientTextDemo;
